/*
The documentation is available here:
https://shadowshell.io/phantom-pass/8-ptrace-deny.html
*/

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
#include <set>

using namespace llvm;

namespace {

class PtraceDenyPass : public PassInfoMixin<PtraceDenyPass> {
private:
  std::set<std::string> FunctionNames;

public:
  PtraceDenyPass() = default;

  PtraceDenyPass(std::set<std::string> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    FunctionCallee PtraceFn = getPtraceFunction(M);

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration()) {
        continue;
      }

      if (injectPtraceCall(F, PtraceFn)) {
        Changed = true;
        outs() << "PtraceDenyPass: Injected ptrace into function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  FunctionCallee getPtraceFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    // Function signature:
    // int ptrace(int _request, pid_t _pid, caddr_t _addr, int _data);
    // int ptrace(int _request, int _pid, char* _addr, int _data);
    // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/ptrace.2.html
    FunctionType *PtraceTy =
        FunctionType::get(Type::getInt32Ty(Ctx),
                          {Type::getInt32Ty(Ctx), Type::getInt32Ty(Ctx),
                           PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
                          false);

    return M.getOrInsertFunction("ptrace", PtraceTy);
  }

  bool injectPtraceCall(Function &F, FunctionCallee PtraceFn) {
    LLVMContext &Ctx = F.getContext();
    BasicBlock &EntryBB = F.getEntryBlock();

    IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());

    // #define PT_DENY_ATTACH  31
    Value *Request = Builder.getInt32(31);
    Value *Pid = Builder.getInt32(0);
    Value *Addr = ConstantPointerNull::get(PointerType::getUnqual(Ctx));
    Value *Data = Builder.getInt32(0);

    Builder.CreateCall(PtraceFn, {Request, Pid, Addr, Data});

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

            std::set<std::string> Functions(Parts.begin(), Parts.end());

            MPM.addPass(PtraceDenyPass(Functions));
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
