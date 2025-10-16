/*
The documentation is available here:
https://shadowshell.io/phantom-pass/9-ptrace-deny-asm.html
*/

#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

namespace {

class PtraceDenyPass : public PassInfoMixin<PtraceDenyPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;

public:
  PtraceDenyPass() = default;

  PtraceDenyPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration()) {
        continue;
      }

      if (injectPtraceAsm(F)) {
        Changed = true;
        outs() << "PtraceDenyPass: Injected ptrace asm into function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  bool injectPtraceAsm(Function &F) {
    LLVMContext &Ctx = F.getContext();
    BasicBlock &EntryBB = F.getEntryBlock();

    IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());

    // ptrace(PT_DENY_ATTACH, 0, 0, 0);
    // https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/syscalls.master
    std::string AsmStr = "stp x0, x1, [sp, #-16]!\n"
                         "stp x2, x3, [sp, #-16]!\n"
                         "mov x0, #31\n"
                         "mov x1, #0\n"
                         "mov x2, #0\n"
                         "mov x3, #0\n"
                         "mov x16, #26\n"
                         "svc #0x80\n"
                         "ldp x2, x3, [sp], #16\n"
                         "ldp x0, x1, [sp], #16\n";

    FunctionType *AsmFnTy = FunctionType::get(Type::getVoidTy(Ctx), false);

    InlineAsm *PtraceAsm = InlineAsm::get(AsmFnTy, AsmStr,
                                          "",   // Constraints
                                          true, // hasSideEffects
                                          false // isAlignStack
    );

    Builder.CreateCall(PtraceAsm);

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "ptrace-deny") {
          MPM.addPass(PtraceDenyPass());
          return true;
        }

        if (Name.consume_front("ptrace-deny<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(PtraceDenyPass(std::move(Functions)));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "PtraceDenyPass", LLVM_VERSION_STRING,
          registerPass};
}