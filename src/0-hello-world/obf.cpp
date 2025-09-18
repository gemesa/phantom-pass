// Simple LLVM pass that inserts a puts("Hello, world!") call into main().

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class HelloWorldPass : public PassInfoMixin<HelloWorldPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    bool Changed = false;

    Function *MainFunc = M.getFunction("main");
    if (!MainFunc) {
      errs() << "HelloWorldPass: No main function found\n";
      return PreservedAnalyses::all();
    }

    injectHelloWorld(M, *MainFunc);

    outs() << "HelloWorldPass: Successfully injected puts(\"Hello, world!\") "
              "into main\n";

    return PreservedAnalyses::none();
  }

private:
  void injectHelloWorld(Module &M, Function &MainFunc) {
    LLVMContext &Ctx = M.getContext();
    BasicBlock &EntryBB = MainFunc.getEntryBlock();

    // We insert before the terminator.
    // Alternatively, we could insert at the first safe insertion point:
    // IRBuilder<> Builder(&*EntryBB.getFirstInsertionPt());
    Instruction *Terminator = EntryBB.getTerminator();
    IRBuilder<> Builder(Terminator);

    // Create an anonymous string as we do not care about the name now.
    Constant *HelloStr = Builder.CreateGlobalString("Hello, world!");

    Function *PutsFunc = getOrCreatePutsFunction(M);

    Builder.CreateCall(PutsFunc, {HelloStr});
  }

  Function *getOrCreatePutsFunction(Module &M) {
    Function *PutsFunc = M.getFunction("puts");
    if (PutsFunc) {
      return PutsFunc;
    }

    LLVMContext &Ctx = M.getContext();

    // Function signature: int puts(i8*)
    FunctionType *PutsFT = FunctionType::get(
        Type::getInt32Ty(Ctx), {PointerType::getUnqual(Ctx)}, false);

    // puts is defined in libc.
    PutsFunc = Function::Create(PutsFT, Function::ExternalLinkage, "puts", M);

    return PutsFunc;
  }
};

} // anonymous namespace

static void registerHelloWorldPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "hello-world") {
          MPM.addPass(HelloWorldPass());
          return true;
        }
        return false;
      });
}

// Plugin registration: makes this pass loadable with opt.
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "HelloWorldPass", LLVM_VERSION_STRING,
          registerHelloWorldPass};
}