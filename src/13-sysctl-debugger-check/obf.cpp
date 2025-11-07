/*
The documentation is available here:
https://shadowshell.io/phantom-pass/13-sysctl-debugger-check.html
*/

#include "llvm/ADT/SmallSet.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <sys/sysctl.h>

using namespace llvm;

namespace {

class DebuggerCheckPass : public PassInfoMixin<DebuggerCheckPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;

public:
  DebuggerCheckPass() = default;

  DebuggerCheckPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    Function *CheckFn = createDebuggerCheckFunction(M);

    for (Function &F : M) {
      if (&F == CheckFn) {
        continue;
      }

      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration()) {
        continue;
      }

      if (injectDebuggerCheckCall(F, CheckFn)) {
        Changed = true;
        outs() << "DebuggerCheckPass: Injected sysctl into function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  FunctionCallee getSysctlFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    // Function signature:
    // int sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void
    // *newp, size_t newlen);
    // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/sysctl.3.html
    FunctionType *SysctlTy =
        FunctionType::get(Type::getInt32Ty(Ctx),        // int
                          {PointerType::getUnqual(Ctx), // int *name
                           Type::getInt32Ty(Ctx),       // u_int namelen
                           PointerType::getUnqual(Ctx), // void *oldp
                           PointerType::getUnqual(Ctx), // size_t *oldlenp
                           PointerType::getUnqual(Ctx), // void *newp
                           Type::getInt64Ty(Ctx)},      // size_t newlen
                          false);

    return M.getOrInsertFunction("sysctl", SysctlTy);
  }

  FunctionCallee getGetpidFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    // Function signature:
    // pid_t getpid(void);
    // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/getpid.2.html
    FunctionType *GetpidTy =
        FunctionType::get(Type::getInt32Ty(Ctx), {}, false);

    return M.getOrInsertFunction("getpid", GetpidTy);
  }

  FunctionCallee getExitFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    // Function signature:
    // void exit(int status);
    // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/exit.3.html
    FunctionType *ExitTy =
        FunctionType::get(Type::getVoidTy(Ctx), {Type::getInt32Ty(Ctx)}, false);

    return M.getOrInsertFunction("exit", ExitTy);
  }

  Function *createDebuggerCheckFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    if (Function *Existing = M.getFunction("__check_debugger")) {
      return Existing;
    }

    FunctionType *FuncTy = FunctionType::get(Type::getVoidTy(Ctx), {}, false);
    Function *CheckFn = Function::Create(FuncTy, Function::InternalLinkage,
                                         "__check_debugger", M);

    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", CheckFn);
    BasicBlock *DebuggedBB = BasicBlock::Create(Ctx, "debugged", CheckFn);
    BasicBlock *NotDebuggedBB =
        BasicBlock::Create(Ctx, "not_debugged", CheckFn);

    IRBuilder<> Builder(EntryBB);

    FunctionCallee SysctlFn = getSysctlFunction(M);
    FunctionCallee GetpidFn = getGetpidFunction(M);

    const size_t KINFO_PROC_SIZE = sizeof(struct kinfo_proc);

    /*
    struct kinfo_proc {
            struct  extern_proc kp_proc;
      ...
    */

    /*
    struct extern_proc {
      ...
            int     p_flag;
    */

    const size_t P_FLAG_OFFSET = offsetof(struct kinfo_proc, kp_proc) +
                                 offsetof(struct extern_proc, p_flag);

    // int mib[4];
    ArrayType *MibArrayTy = ArrayType::get(Builder.getInt32Ty(), 4);
    AllocaInst *MibArray = Builder.CreateAlloca(MibArrayTy, nullptr, "mib");

    // mib[0] = CTL_KERN;
    // mib[1] = KERN_PROC;
    // mib[2] = KERN_PROC_PID;
    // mib[3] = getpid();
    Value *Mib0 = Builder.CreateConstGEP2_32(MibArrayTy, MibArray, 0, 0);
    Value *Mib1 = Builder.CreateConstGEP2_32(MibArrayTy, MibArray, 0, 1);
    Value *Mib2 = Builder.CreateConstGEP2_32(MibArrayTy, MibArray, 0, 2);
    Value *Mib3 = Builder.CreateConstGEP2_32(MibArrayTy, MibArray, 0, 3);

    Builder.CreateStore(Builder.getInt32(CTL_KERN), Mib0);
    Builder.CreateStore(Builder.getInt32(KERN_PROC), Mib1);
    Builder.CreateStore(Builder.getInt32(KERN_PROC_PID), Mib2);

    Value *Pid = Builder.CreateCall(GetpidFn);
    Builder.CreateStore(Pid, Mib3);

    // struct kinfo_proc info;
    ArrayType *InfoStructTy =
        ArrayType::get(Builder.getInt8Ty(), KINFO_PROC_SIZE);
    AllocaInst *InfoStruct =
        Builder.CreateAlloca(InfoStructTy, nullptr, "info");

    // info.kp_proc.p_flag = 0;
    // Just zero init the whole struct instead.
    Builder.CreateMemSet(InfoStruct, Builder.getInt8(0), KINFO_PROC_SIZE,
                         Align(8));

    // size_t size;
    // size = sizeof(info);
    AllocaInst *SizeVar =
        Builder.CreateAlloca(Builder.getInt64Ty(), nullptr, "size");
    Builder.CreateStore(Builder.getInt64(KINFO_PROC_SIZE), SizeVar);

    // sysctl(mib, sizeof(mib) / sizeof(*mib), &info, &size, NULL, 0);
    // sysctl(mib, 4, &info, &size, NULL, 0);
    Value *MibPtr = Builder.CreateBitCast(MibArray, Builder.getPtrTy());
    Value *InfoPtr = Builder.CreateBitCast(InfoStruct, Builder.getPtrTy());
    Value *NullPtr = ConstantPointerNull::get(Builder.getPtrTy());

    Builder.CreateCall(SysctlFn, {MibPtr, Builder.getInt32(4), InfoPtr, SizeVar,
                                  NullPtr, Builder.getInt64(0)});

    // (info.kp_proc.p_flag & P_TRACED) != 0 )
    Value *PFlagPtr =
        Builder.CreateConstGEP1_32(Builder.getInt8Ty(), InfoPtr, P_FLAG_OFFSET);
    Value *PFlag = Builder.CreateLoad(Builder.getInt32Ty(), PFlagPtr, "p_flag");

    Value *TracedFlag = Builder.getInt32(P_TRACED);
    Value *Masked = Builder.CreateAnd(PFlag, TracedFlag);
    Value *IsDebugged = Builder.CreateICmpNE(Masked, Builder.getInt32(0));

    Builder.CreateCondBr(IsDebugged, DebuggedBB, NotDebuggedBB);

    // Exit if debugger is detected.
    Builder.SetInsertPoint(DebuggedBB);
    FunctionCallee ExitFn = getExitFunction(M);
    Builder.CreateCall(ExitFn, {Builder.getInt32(1)});
    Builder.CreateUnreachable();

    Builder.SetInsertPoint(NotDebuggedBB);
    Builder.CreateRetVoid();

    return CheckFn;
  }

  // https://developer.apple.com/library/archive/qa/qa1361/_index.html
  bool injectDebuggerCheckCall(Function &F, Function *CheckFn) {
    BasicBlock &EntryBB = F.getEntryBlock();
    IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());

    Builder.CreateCall(CheckFn, {});

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "sysctl-debugger-check") {
          MPM.addPass(DebuggerCheckPass());
          return true;
        }

        if (Name.consume_front("sysctl-debugger-check<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(DebuggerCheckPass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "DebuggerCheckPass", LLVM_VERSION_STRING,
          registerPass};
}
