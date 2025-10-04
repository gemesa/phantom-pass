/*
An LLVM pass that replaces `42` constants with equivalent obfuscated
[MBA](https://www.usenix.org/conference/usenixsecurity21/presentation/liu-binbin)
instruction sequences. These are more difficult to analyze at the expense of an
increased runtime penalty.

A [helper
script](https://github.com/gemesa/phantom-pass/tree/main/src/util/mba_solver.py)
has been implemented to support solving MBA problems. The MBA implemented in
this pass has been constructed with the following parameters:

```
$ python3 mba_solver.py 42 8
TGT: 42
MBA: 20000*x + 20000*y - 20000*(x&y) - 20000*(x|y) - 214
(0.11s)
```

Where `42` is the constant we want to obfuscate, 8 is the bitwidth and the
coefficient bound is unspecified (default: 20000).

Known limitations:
- increased runtime penalty
*/

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
#include <vector>

using namespace llvm;

namespace {

class MbaConstPass : public PassInfoMixin<MbaConstPass> {
public:
  MbaConstPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    // The initializer value does not matter, it can be anything.
    // x is defined as a GV to prevent constant folding.
    GlobalVariable *GVX = new GlobalVariable(
        M, Type::getInt32Ty(M.getContext()), true, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt32Ty(M.getContext()), 13), "x");

    // The initializer value does not matter, it can be anything.
    // y is defined as a GV to prevent constant folding.
    GlobalVariable *GVY = new GlobalVariable(
        M, Type::getInt32Ty(M.getContext()), true, GlobalValue::InternalLinkage,
        ConstantInt::get(Type::getInt32Ty(M.getContext()), 21), "y");

    std::vector<std::pair<Instruction *, unsigned>> InstConstPair =
        locateConsts(M);

    Changed = replaceConsts(InstConstPair, GVX, GVY);

    if (Changed) {
      outs() << "MbaConstPass: Replaced " << InstConstPair.size()
             << " instance(s) of constant 42\n";
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  std::vector<std::pair<Instruction *, unsigned>> locateConsts(Module &M) {
    std::vector<std::pair<Instruction *, unsigned>> InstConstPair;
    for (Function &F : M) {
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          for (unsigned i = 0; i < I.getNumOperands(); ++i) {
            if (auto *CI = dyn_cast<ConstantInt>(I.getOperand(i))) {
              if (CI->getSExtValue() == 42) {
                InstConstPair.push_back({&I, i});
              }
            }
          }
        }
      }
    }
    return InstConstPair;
  }

  /*
  TGT: 42
  MBA: 20000*x + 20000*y - 20000*(x&y) - 20000*(x|y) - 214
  */
  bool replaceConsts(
      const std::vector<std::pair<Instruction *, unsigned>> &InstConstPair,
      GlobalVariable *GVX, GlobalVariable *GVY) {
    for (auto &Pair : InstConstPair) {
      Instruction *I = Pair.first;
      unsigned OpIdx = Pair.second;

      IRBuilder<> Builder(I);

      Value *X = Builder.CreateLoad(Builder.getInt32Ty(), GVX, "x");
      Value *Y = Builder.CreateLoad(Builder.getInt32Ty(), GVY, "y");

      Value *Term0 = Builder.CreateMul(Builder.getInt32(20000), X, "term0");
      Value *Term1 = Builder.CreateMul(Builder.getInt32(20000), Y, "term1");

      Value *AndResult = Builder.CreateAnd(X, Y, "and");
      Value *Term2 =
          Builder.CreateMul(Builder.getInt32(20000), AndResult, "term2");

      Value *OrResult = Builder.CreateOr(X, Y, "or");
      Value *Term3 =
          Builder.CreateMul(Builder.getInt32(20000), OrResult, "term3");

      Value *Sum = Builder.CreateAdd(Term0, Term1, "sum1");
      Sum = Builder.CreateSub(Sum, Term2, "sum2");
      Sum = Builder.CreateSub(Sum, Term3, "sum3");
      Value *Result = Builder.CreateSub(Sum, Builder.getInt32(214), "result");

      // Truncate to 8 bit (applies mod 256), then extend back to 32 bit.
      // This makes -214 wrap to 42.
      Value *Result8 =
          Builder.CreateTrunc(Result, Builder.getInt8Ty(), "trunc8");
      Value *Result32 =
          Builder.CreateZExt(Result8, Builder.getInt32Ty(), "zext32");

      I->setOperand(OpIdx, Result32);
    }
    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "mba-const") {
          MPM.addPass(MbaConstPass());
          return true;
        }
        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MbaConstPass", LLVM_VERSION_STRING,
          registerPass};
}