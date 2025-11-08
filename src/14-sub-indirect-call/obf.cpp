/*
The documentation is available here:
https://shadowshell.io/phantom-pass/14-sub-indirect-call.html
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

class SubIndirectCallPass : public PassInfoMixin<SubIndirectCallPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<RandomNumberGenerator> RNG;

  uint64_t getRandomOffset() {
    std::uniform_int_distribution<uint64_t> Dist(
        1, std::numeric_limits<uint8_t>::max());
    return Dist(*RNG);
  }

public:
  SubIndirectCallPass() = default;

  SubIndirectCallPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    RNG = M.createRNG("sub-indirect-call");

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration() || F.isIntrinsic()) {
        continue;
      }

      if (replaceCalls(F)) {
        Changed = true;
        outs() << "SubIndirectCallPass: calls replaced in function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  bool replaceCalls(Function &F) {

    Module &M = *F.getParent();
    LLVMContext &Ctx = M.getContext();

    struct CallSiteInfo {
      Constant *Offset;
      Constant *EncodedAddr;
      CallInst *DirectCall;
    };

    SmallVector<CallSiteInfo, 16> CallSites;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        auto *CI = dyn_cast<CallInst>(&I);
        if (!CI) {
          continue;
        }
        Function *Callee = CI->getCalledFunction();

        if (!Callee || Callee->isIntrinsic() ||
            Callee->hasFnAttribute(Attribute::AlwaysInline)) {
          continue;
        }

        uint64_t Offset = getRandomOffset();

        Constant *FuncAddr =
            ConstantExpr::getPtrToInt(Callee, Type::getInt64Ty(Ctx));
        Constant *OffsetConst = ConstantInt::get(Type::getInt64Ty(Ctx), Offset);
        Constant *EncodedAddr = ConstantExpr::getSub(FuncAddr, OffsetConst);

        CallSites.push_back({OffsetConst, EncodedAddr, CI});
      }
    }

    if (CallSites.empty()) {
      return false;
    }

    SmallVector<Constant *, 16> Offsets;
    SmallVector<Constant *, 16> EncodedAddrs;

    for (const auto &Site : CallSites) {
      Offsets.push_back(Site.Offset);
      EncodedAddrs.push_back(Site.EncodedAddr);
    }

    ArrayType *ArrTy = ArrayType::get(Type::getInt64Ty(Ctx), Offsets.size());

    auto *GVOffsets = new GlobalVariable(
        M, ArrTy, true, GlobalValue::InternalLinkage,
        ConstantArray::get(ArrTy, Offsets), ".sub_icall.offsets");

    auto *GVEncodedAddrs = new GlobalVariable(
        M, ArrTy, true, GlobalValue::InternalLinkage,
        ConstantArray::get(ArrTy, EncodedAddrs), ".sub_icall.encoded");

    IRBuilder<> Builder(Ctx);

    Constant *Zero = ConstantInt::get(Builder.getInt64Ty(), 0);

    for (auto [Idx, Site] : enumerate(CallSites)) {
      Builder.SetInsertPoint(Site.DirectCall);
      Constant *Index = ConstantInt::get(Builder.getInt64Ty(), Idx);

      Value *OffsetPtr =
          Builder.CreateInBoundsGEP(ArrTy, GVOffsets, {Zero, Index});
      LoadInst *Offset = Builder.CreateLoad(Builder.getInt64Ty(), OffsetPtr);
      // Mark load as volatile to prevent simplification of the indirect calls
      // by the optimizer.
      Offset->setVolatile(true);

      Value *EncodedAddrPtr =
          Builder.CreateInBoundsGEP(ArrTy, GVEncodedAddrs, {Zero, Index});
      LoadInst *EncodedAddr =
          Builder.CreateLoad(Builder.getInt64Ty(), EncodedAddrPtr);
      // Mark load as volatile to prevent simplification of the indirect calls
      // by the optimizer.
      EncodedAddr->setVolatile(true);

      Value *DecodedAddr = Builder.CreateAdd(EncodedAddr, Offset);
      Value *FuncPtr = Builder.CreateIntToPtr(DecodedAddr, Builder.getPtrTy());

      outs() << "Replacing call to "
             << Site.DirectCall->getCalledFunction()->getName() << "\n";

      Site.DirectCall->setCalledOperand(FuncPtr);
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "sub-indirect-call") {
          MPM.addPass(SubIndirectCallPass());
          return true;
        }

        if (Name.consume_front("sub-indirect-call<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(SubIndirectCallPass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "SubIndirectCallPass", LLVM_VERSION_STRING,
          registerPass};
}
