/*
https://shadowshell.io/phantom-pass/12-frida-deny-with-runtime-check.html
*/
#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

namespace {

class FridaDenyPass : public PassInfoMixin<FridaDenyPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  SmallSet<Function *, 16> CheckerFunctions;

public:
  FridaDenyPass() = default;

  FridaDenyPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    const auto &TT = Triple(M.getTargetTriple());
    if (!TT.isAArch64()) {
      errs() << "FridaDenyPass: Only AArch64 is supported\n";
      return PreservedAnalyses::all();
    }

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration()) {
        continue;
      }

      if (F.getName() == "main") {
        errs() << "FridaDenyPass: Protecting " << F.getName()
               << " is not supported\n";
        continue;
      }

      if (F.hasPrologueData()) {
        errs() << "FridaDenyPass: Function " << F.getName()
               << " already has some prologue data\n";
        continue;
      }

      if (injectFridaPrologue(F)) {
        Changed = true;
        outs() << "FridaDenyPass: Injected frida deny prologue into function '"
               << F.getName() << "'\n";

        FunctionCallee MemcmpFn = getMemcmpFunction(M);
        FunctionCallee PrintfFn = getPrintfFunction(M);
        FunctionCallee ExitFn = getExitFunction(M);

        Function *CheckerFn =
            createCheckerFunction(M, F, MemcmpFn, PrintfFn, ExitFn);
        if (CheckerFn) {
          CheckerFunctions.insert(CheckerFn);
          outs() << "  + Created checker function: " << CheckerFn->getName()
                 << "()\n";
        }
      }
    }

    if (!CheckerFunctions.empty()) {
      if (injectCheckersIntoMain(M, CheckerFunctions)) {
        Changed = true;
        outs() << "FridaDenyPass: Injected " << CheckerFunctions.size()
               << " checker call(s) into main()\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  // https://cplusplus.com/reference/cstring/memcmp/
  // int memcmp ( const void * ptr1, const void * ptr2, size_t num );
  FunctionCallee getMemcmpFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *MemcmpTy =
        FunctionType::get(Type::getInt32Ty(Ctx),
                          {PointerType::getUnqual(Ctx),
                           PointerType::getUnqual(Ctx), Type::getInt64Ty(Ctx)},
                          false);
    return M.getOrInsertFunction("memcmp", MemcmpTy);
  }

  // https://cplusplus.com/reference/cstdio/printf/
  // int printf ( const char * format, ... );
  FunctionCallee getPrintfFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *PrintfTy = FunctionType::get(
        Type::getInt32Ty(Ctx), {PointerType::getUnqual(Ctx)}, true);
    return M.getOrInsertFunction("printf", PrintfTy);
  }

  // https://cplusplus.com/reference/cstdlib/exit/
  // void exit (int status);
  FunctionCallee getExitFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();
    FunctionType *ExitTy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);
    return M.getOrInsertFunction("exit", ExitTy);
  }

  Constant *createGlobalString(Module &M, StringRef Str) {
    LLVMContext &Ctx = M.getContext();
    Constant *StrConstant = ConstantDataArray::getString(Ctx, Str, true);

    GlobalVariable *GV =
        new GlobalVariable(M, StrConstant->getType(), true,
                           GlobalValue::PrivateLinkage, StrConstant, ".str");

    return GV;
  }

  bool injectFridaPrologue(Function &F) {
    uint8_t PrologueBytes[] = {
        // $ echo "mov x16, x16" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x16, x16 // encoding: [0xf0,0x03,0x10,0xaa]
        0xF0, 0x03, 0x10, 0xAA,
        // $ echo "mov x17, x17" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x17, x17 // encoding: [0xf1,0x03,0x11,0xaa]
        0xF1, 0x03, 0x11, 0xAA};

    LLVMContext &Ctx = F.getContext();
    auto *Prologue = ConstantDataVector::getRaw(
        StringRef(reinterpret_cast<const char *>(PrologueBytes),
                  sizeof(PrologueBytes)),
        sizeof(PrologueBytes), Type::getInt8Ty(Ctx));

    F.setPrologueData(Prologue);
    return true;
  }

  Function *createCheckerFunction(Module &M, Function &TargetFunc,
                                  FunctionCallee MemcmpFn,
                                  FunctionCallee PrintfFn,
                                  FunctionCallee ExitFn) {
    LLVMContext &Ctx = M.getContext();

    uint8_t ExpectedBytes[] = {
        // $ echo "mov x16, x16" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x16, x16 // encoding: [0xf0,0x03,0x10,0xaa]
        0xF0, 0x03, 0x10, 0xAA,
        // $ echo "mov x17, x17" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x17, x17 // encoding: [0xf1,0x03,0x11,0xaa]
        0xF1, 0x03, 0x11, 0xAA};

    std::string GlobalName = ".expected_prologue_" + TargetFunc.getName().str();
    Constant *ExpectedData = ConstantDataArray::get(
        Ctx, ArrayRef<uint8_t>(ExpectedBytes, sizeof(ExpectedBytes)));

    GlobalVariable *ExpectedGlobal = new GlobalVariable(
        M, ExpectedData->getType(), true, GlobalValue::PrivateLinkage,
        ExpectedData, GlobalName);

    std::string CheckerName = "__check_" + TargetFunc.getName().str();
    // void __check_<name>(void);
    FunctionType *CheckerTy = FunctionType::get(Type::getVoidTy(Ctx), false);
    Function *CheckerFn =
        Function::Create(CheckerTy, Function::ExternalLinkage, CheckerName, M);

    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", CheckerFn);
    BasicBlock *SuccessBB = BasicBlock::Create(Ctx, "success", CheckerFn);
    BasicBlock *FailBB = BasicBlock::Create(Ctx, "fail", CheckerFn);

    IRBuilder<> Builder(EntryBB);

    Value *FuncPtr =
        Builder.CreateBitCast(&TargetFunc, PointerType::getUnqual(Ctx));

    Value *ExpectedPtr =
        Builder.CreateBitCast(ExpectedGlobal, PointerType::getUnqual(Ctx));

    Value *CmpResult = Builder.CreateCall(
        MemcmpFn, {FuncPtr, ExpectedPtr, Builder.getInt64(8)});

    Value *IsMatch = Builder.CreateICmpEQ(CmpResult, Builder.getInt32(0));
    Builder.CreateCondBr(IsMatch, SuccessBB, FailBB);

    Builder.SetInsertPoint(SuccessBB);
    Builder.CreateRetVoid();

    Builder.SetInsertPoint(FailBB);

    std::string ErrorMsg = "\nPatching/hooking detected.\n"
                           "Prologue of function '" +
                           TargetFunc.getName().str() +
                           "' has been modified.\n"
                           "Exiting...\n\n";

    Constant *ErrorStr = createGlobalString(M, ErrorMsg);

    Builder.CreateCall(PrintfFn, {ErrorStr});
    Builder.CreateCall(ExitFn, {Builder.getInt32(1)});
    Builder.CreateUnreachable();

    return CheckerFn;
  }

  bool
  injectCheckersIntoMain(Module &M,
                         const SmallSet<Function *, 16> &CheckerFunctions) {
    Function *MainFn = M.getFunction("main");
    if (!MainFn) {
      errs() << "Warning: main() not found, cannot inject checkers\n";
      return false;
    }

    if (MainFn->isDeclaration()) {
      errs() << "Warning: main() is a declaration, cannot inject checkers\n";
      return false;
    }

    BasicBlock &EntryBB = MainFn->getEntryBlock();
    IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());

    for (Function *CheckerFn : CheckerFunctions) {
      Builder.CreateCall(CheckerFn);
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "frida-deny-check") {
          MPM.addPass(FridaDenyPass());
          return true;
        }

        if (Name.consume_front("frida-deny-check<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions;
            for (StringRef Part : Parts) {
              if (!Part.empty()) {
                Functions.insert(Part);
              }
            }

            MPM.addPass(FridaDenyPass(std::move(Functions)));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FridaDenyPass", LLVM_VERSION_STRING,
          registerPass};
}