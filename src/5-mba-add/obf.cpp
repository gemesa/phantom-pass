// TODO

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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <vector>

using namespace llvm;

namespace {

class MbaAddPass : public PassInfoMixin<MbaAddPass> {
public:
  MbaAddPass() = default;

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    std::vector<BinaryOperator *> AddOps = locateAddOps(M);

    if (AddOps.empty()) {
      outs() << "MbaAddPass: Could not locate any add operators\n";
      return PreservedAnalyses::all();
    }

    Changed = replaceAddOps(M, AddOps);

    if (Changed) {
      outs() << "MbaAddPass: Replaced " << AddOps.size() << " add operators\n";
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  std::vector<BinaryOperator *> locateAddOps(Module &M) {
    std::vector<BinaryOperator *> AddOps;

    for (Function &F : M) {
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (auto *BinOp = dyn_cast<BinaryOperator>(&I)) {
            if (BinOp->getOpcode() == Instruction::Add) {
              AddOps.push_back(BinOp);
            }
          }
        }
      }
    }

    return AddOps;
  }

  /*
  TGT: x+y
  MBA: 200*x + 200*y - 200*(x&y) - 198*(x|y) - 1*(x^y)
  */
  bool replaceAddOps(Module &, const std::vector<BinaryOperator *> &AddOps) {
    for (BinaryOperator *BinOp : AddOps) {
      IRBuilder<> Builder(BinOp);

      Value *X = BinOp->getOperand(0);
      Value *Y = BinOp->getOperand(1);

      Value *Term0 = Builder.CreateMul(Builder.getInt32(200), X, "term0");

      Value *Term1 = Builder.CreateMul(Builder.getInt32(200), Y, "term1");

      Value *AndResult = Builder.CreateAnd(X, Y, "and");
      Value *Term2 =
          Builder.CreateMul(Builder.getInt32(200), AndResult, "term2");

      Value *OrResult = Builder.CreateOr(X, Y, "or");
      Value *Term3 =
          Builder.CreateMul(Builder.getInt32(198), OrResult, "term3");

      Value *Term4 = Builder.CreateXor(X, Y, "xor");

      Value *Sum = Builder.CreateAdd(Term0, Term1);
      Sum = Builder.CreateSub(Sum, Term2);
      Sum = Builder.CreateSub(Sum, Term3);
      Value *Result = Builder.CreateSub(Sum, Term4, "result");

      BinOp->replaceAllUsesWith(Result);
      BinOp->eraseFromParent();
    }
    return true;
  }
};
} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "mba-add") {
          MPM.addPass(MbaAddPass());
          return true;
        }
        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "MbaAddPass", LLVM_VERSION_STRING,
          registerPass};
}