/*
The documentation is available here:
https://shadowshell.io/phantom-pass/16-indirect-branch.html
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

class IndirectBranchPass : public PassInfoMixin<IndirectBranchPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  IndirectBranchPass() = default;

  IndirectBranchPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    RNG = M.createRNG("indirect-branch");

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration() || F.isIntrinsic()) {
        continue;
      }

      if (replaceBranches(F)) {
        Changed = true;
        outs() << "IndirectBranchPass: branches replaced in function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  bool replaceBranches(Function &F) {
    LLVMContext &Ctx = F.getContext();
    Module &M = *F.getParent();
    const DataLayout &DL = M.getDataLayout();

    // Collect branch instructions and successor blocks.
    SmallVector<Instruction *, 16> BranchesToReplace;
    SmallPtrSet<BasicBlock *, 32> SuccessorBlocks;

    for (BasicBlock &BB : F) {
      Instruction *Term = BB.getTerminator();

      // TODO: Add support for switch instructions.
      if (auto *BI = dyn_cast<BranchInst>(Term)) {
        BranchesToReplace.push_back(Term);

        for (BasicBlock *Succ : successors(BI)) {
          SuccessorBlocks.insert(Succ);
        }
      }
    }

    if (BranchesToReplace.empty()) {
      errs() << "IndirectBranchPass: there are no branches to replace in "
                "function '"
             << F.getName() << "'\n";
      return false;
    }

    // Create the jump table.
    SmallVector<Constant *, 32> BlockAddresses;
    for (BasicBlock *BB : SuccessorBlocks) {
      BlockAddresses.push_back(BlockAddress::get(BB));
    }

    std::shuffle(BlockAddresses.begin(), BlockAddresses.end(), *RNG);

    ArrayType *TableTy =
        ArrayType::get(PointerType::getUnqual(Ctx), BlockAddresses.size());

    GlobalVariable *JumpTable = new GlobalVariable(
        M, TableTy, true, GlobalValue::PrivateLinkage,
        ConstantArray::get(TableTy, BlockAddresses), "jump_table");

    // Build a mapping from BasicBlock to its index in the jump table.
    DenseMap<BasicBlock *, unsigned> BlockToIndex;
    for (auto [Idx, Addr] : enumerate(BlockAddresses)) {
      BasicBlock *BB = cast<BlockAddress>(Addr)->getBasicBlock();
      BlockToIndex[BB] = Idx;
    }

    // Replace direct branches with indirect ones.
    Type *IndexTy = DL.getIndexType(JumpTable->getType());
    Constant *Zero = ConstantInt::get(IndexTy, 0);

    for (Instruction *Term : BranchesToReplace) {
      auto *BI = cast<BranchInst>(Term);
      IRBuilder<> Builder(Term);

      Value *TargetAddr = nullptr;

      if (BI->isUnconditional()) {
        unsigned Idx = BlockToIndex[BI->getSuccessor(0)];
        TargetAddr = Builder.CreateInBoundsGEP(
            TableTy, JumpTable, {Zero, ConstantInt::get(IndexTy, Idx)});
      } else {
        // Conditional branch.
        unsigned Idx = BlockToIndex[BI->getSuccessor(0)];
        Value *TrueEntry = Builder.CreateInBoundsGEP(
            TableTy, JumpTable, {Zero, ConstantInt::get(IndexTy, Idx)});
        Idx = BlockToIndex[BI->getSuccessor(1)];
        Value *FalseEntry = Builder.CreateInBoundsGEP(
            TableTy, JumpTable, {Zero, ConstantInt::get(IndexTy, Idx)});
        TargetAddr =
            Builder.CreateSelect(BI->getCondition(), TrueEntry, FalseEntry);
      }

      Value *LoadedAddr = Builder.CreateLoad(PointerType::getUnqual(Ctx),
                                             TargetAddr, "indirect_target");

      IndirectBrInst *IndBr =
          Builder.CreateIndirectBr(LoadedAddr, BI->getNumSuccessors());
      // This helps LLVM to know the graph structure.
      for (BasicBlock *Succ : successors(BI)) {
        IndBr->addDestination(Succ);
      }
      Term->eraseFromParent();
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "indirect-branch") {
          MPM.addPass(IndirectBranchPass());
          return true;
        }

        if (Name.consume_front("indirect-branch<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(IndirectBranchPass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "IndirectBranchPass", LLVM_VERSION_STRING,
          registerPass};
}
