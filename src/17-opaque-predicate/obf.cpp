/*
The documentation is available here:
https://shadowshell.io/phantom-pass/17-opaque-predicate.html
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
#include "llvm/Support/RandomNumberGenerator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class OpaquePredicatePass : public PassInfoMixin<OpaquePredicatePass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  OpaquePredicatePass() = default;

  OpaquePredicatePass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    RNG = M.createRNG("opaque-predicate");

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration() || F.isIntrinsic()) {
        continue;
      }

      if (obfuscateBranches(F)) {
        Changed = true;
        outs() << "OpaquePredicatePass: predicates replaced in function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // 7y^2 - 1 != x^2
  Value *createPredicate_7y2m1_neq_x2(IRBuilder<> &Builder, Value *X,
                                      Value *Y) {
    Value *YSq = Builder.CreateMul(Y, Y, "y2");
    Value *XSq = Builder.CreateMul(X, X, "x2");
    Type *Ty = X->getType();
    Value *Seven = ConstantInt::get(Ty, 7);
    Value *One = ConstantInt::get(Ty, 1);
    Value *SevenYSq = Builder.CreateMul(Seven, YSq, "7y2");
    Value *Left = Builder.CreateSub(SevenYSq, One, "7y2m1");
    return Builder.CreateICmpNE(Left, XSq, "opaque_7y2m1_neq_x2");
  }

  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // 2|x(x+1) meaning 2 divides x(x+1)
  Value *createPredicate_2_div_xx1(IRBuilder<> &Builder, Value *X) {
    Type *Ty = X->getType();
    Value *One = ConstantInt::get(Ty, 1);
    Value *XPlus1 = Builder.CreateAdd(X, One, "x_plus_1");
    Value *Product = Builder.CreateMul(X, XPlus1, "x_mul_x1");
    // n & 1 is equivalent to n % 2
    Value *Mod2 = Builder.CreateAnd(Product, One, "mod_2");
    Value *Zero = ConstantInt::get(Ty, 0);
    return Builder.CreateICmpEQ(Mod2, Zero, "opaque_2_div_xx1");
  }

  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // 3|x(x+1)(x+2) meaning 3 divides x(x+1)(x+2)
  Value *createPredicate_3_div_xx1x2(IRBuilder<> &Builder, Value *X) {
    Type *Ty = X->getType();
    Value *One = ConstantInt::get(Ty, 1);
    Value *XPlus1 = Builder.CreateAdd(X, One, "x_plus_1");
    Value *Product1 = Builder.CreateMul(X, XPlus1, "x_mul_x1");
    Value *Two = ConstantInt::get(Ty, 2);
    Value *XPlus2 = Builder.CreateAdd(X, Two, "x_plus_2");
    Value *Product2 = Builder.CreateMul(Product1, XPlus2, "x_mul_x1_mul_x2");
    Value *Three = ConstantInt::get(Ty, 3);
    Value *Mod3 = Builder.CreateSRem(Product2, Three, "mod_3");
    Value *Zero = ConstantInt::get(Ty, 0);
    return Builder.CreateICmpEQ(Mod3, Zero, "opaque_3_div_xx1x2");
  }

  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // x^2 > 0
  Value *createPredicate_x2_geq_0(IRBuilder<> &Builder, Value *X) {
    Value *XSq = Builder.CreateMul(X, X, "x2");
    Type *Ty = X->getType();
    Value *Zero = ConstantInt::get(Ty, 0);
    return Builder.CreateICmpSGE(XSq, Zero, "opaque_x2_geq_0");
  }

  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // 7x^2 + 1 ≢ 0 (mod 7) meaning 7x^2 + 1 is not divisible by 7
  Value *createPredicate_7x2p1_mod7(IRBuilder<> &Builder, Value *X) {
    Type *Ty = X->getType();
    Value *One = ConstantInt::get(Ty, 1);
    Value *Seven = ConstantInt::get(Ty, 7);
    Value *XSq = Builder.CreateMul(X, X, "x2");
    Value *SevenXSq = Builder.CreateMul(Seven, XSq, "7x2");
    Value *Expr = Builder.CreateAdd(SevenXSq, One, "7x2p1");
    Value *Mod7 = Builder.CreateSRem(Expr, Seven, "mod_7");
    Value *Zero = ConstantInt::get(Ty, 0);
    return Builder.CreateICmpNE(Mod7, Zero, "opaque_7x2p1_mod7");
  }

  // https://eprint.iacr.org/2017/787.pdf
  // page 5, table 1
  // x^2 + x + 7 ≢ 0 (mod 81) meaning x^2 + x + 7 is not divisible by 81
  Value *createPredicate_x2pxp7_mod81(IRBuilder<> &Builder, Value *X) {
    Type *Ty = X->getType();
    Value *Seven = ConstantInt::get(Ty, 7);
    Value *EightyOne = ConstantInt::get(Ty, 81);
    Value *XSq = Builder.CreateMul(X, X, "x2");
    Value *XSqPlusX = Builder.CreateAdd(XSq, X, "x2px");
    Value *Expr = Builder.CreateAdd(XSqPlusX, Seven, "x2pxp7");
    Value *Mod81 = Builder.CreateSRem(Expr, EightyOne, "mod_81");
    Value *Zero = ConstantInt::get(Ty, 0);
    return Builder.CreateICmpNE(Mod81, Zero, "opaque_x2pxp7_mod81");
  }

  Value *createOpaquePredicate(IRBuilder<> &Builder, Value *X, Value *Y) {
    std::uniform_int_distribution<int> Dist(0, 5);
    int Choice = Dist(*RNG);

    switch (Choice) {
    case 0:
      return createPredicate_7y2m1_neq_x2(Builder, X, Y);
    case 1:
      return createPredicate_2_div_xx1(Builder, X);
    case 2:
      return createPredicate_3_div_xx1x2(Builder, X);
    case 3:
      return createPredicate_x2_geq_0(Builder, X);
    case 4:
      return createPredicate_7x2p1_mod7(Builder, X);
    case 5:
      return createPredicate_x2pxp7_mod81(Builder, X);
    default:
      return createPredicate_2_div_xx1(Builder, X);
    }
  }

  bool obfuscateBranches(Function &F) {
    LLVMContext &Ctx = F.getContext();
    Module &M = *F.getParent();
    Type *I32Ty = Type::getInt32Ty(Ctx);

    // Opaque predicate inputs.
    GlobalVariable *GX =
        new GlobalVariable(M, I32Ty, false, GlobalValue::PrivateLinkage,
                           ConstantInt::get(I32Ty, 13), "opaque_x");
    GlobalVariable *GY =
        new GlobalVariable(M, I32Ty, false, GlobalValue::PrivateLinkage,
                           ConstantInt::get(I32Ty, 37), "opaque_y");

    // Collect conditional branches.
    SmallVector<BranchInst *, 16> ConditionalBranches;

    for (BasicBlock &BB : F) {
      if (auto *BI = dyn_cast<BranchInst>(BB.getTerminator())) {
        if (BI->isConditional()) {
          ConditionalBranches.push_back(BI);
        }
      }
    }

    if (ConditionalBranches.empty()) {
      errs() << "OpaquePredicatePass: no conditional branches in function '"
             << F.getName() << "'\n";
      return false;
    }

    // Replace each conditional branch with an obfuscated version.
    for (BranchInst *BI : ConditionalBranches) {
      IRBuilder<> Builder(BI);

      Value *X = Builder.CreateLoad(I32Ty, GX, "load_x");
      Value *Y = Builder.CreateLoad(I32Ty, GY, "load_y");

      Value *OpaqueCond = createOpaquePredicate(Builder, X, Y);

      Value *OrigCond = BI->getCondition();

      Value *FinalCond = Builder.CreateAnd(OrigCond, OpaqueCond, "obf_cond");

      BI->setCondition(FinalCond);
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "opaque-predicate") {
          MPM.addPass(OpaquePredicatePass());
          return true;
        }

        if (Name.consume_front("opaque-predicate<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(OpaquePredicatePass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "OpaquePredicatePass", LLVM_VERSION_STRING,
          registerPass};
}
