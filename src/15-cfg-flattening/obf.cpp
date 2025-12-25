/*
The documentation is available here:
https://shadowshell.io/phantom-pass/15-cfg-flattening.html
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

class FlattenCfgPass : public PassInfoMixin<FlattenCfgPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<RandomNumberGenerator> RNG;

public:
  FlattenCfgPass() = default;

  FlattenCfgPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    RNG = M.createRNG("flatten-cfg");

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration() || F.isIntrinsic()) {
        continue;
      }

      if (flattenCfg(F)) {
        Changed = true;
        outs() << "FlattenCfgPass: CFG flattened in function '" << F.getName()
               << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  bool flattenCfg(Function &F) {
    LLVMContext &Ctx = F.getContext();

    SmallVector<BasicBlock *, 16> OrigBlocks;

    // Collect all basic blocks that we will flatten.
    for (BasicBlock &BB : F) {
      if (&BB == &F.getEntryBlock()) {
        continue;
      }

      OrigBlocks.push_back(&BB);
    }

    if (OrigBlocks.empty()) {
      outs() << "There are no basic blocks to be flattened.";
      return false;
    }

    /*
    outs() << "Basic blocks to be flattened:\n";
    for (BasicBlock *BB : OrigBlocks) {
      outs() << "  " << BB->getName() << "\n";
    }
    */

    SmallVector<uint32_t, 16> BlockIDs;
    for (uint32_t i = 0; i < OrigBlocks.size(); ++i) {
      BlockIDs.push_back(i);
    }

    // Randomize block IDs.
    std::shuffle(BlockIDs.begin(), BlockIDs.end(), *RNG);

    DenseMap<BasicBlock *, uint32_t> BlockToID;
    for (uint32_t i = 0; i < OrigBlocks.size(); ++i) {
      BlockToID[OrigBlocks[i]] = BlockIDs[i];
    }

    // A state variable holds the ID of the next block to execute.
    BasicBlock *EntryBlock = &F.getEntryBlock();
    IRBuilder<> EntryBuilder(EntryBlock, EntryBlock->begin());
    Value *StateVar =
        EntryBuilder.CreateAlloca(EntryBuilder.getInt32Ty(), nullptr, "state");

    // Find the first block to execute after entry.
    // For now, we assume that the control is transferred via
    // an unconditional branch.
    // TODO: Add support for conditional branch, return, switch, etc.
    BasicBlock *FirstBlock = nullptr;
    Instruction *EntryTerminator = EntryBlock->getTerminator();

    if (auto *Br = dyn_cast<BranchInst>(EntryTerminator)) {
      if (Br->isUnconditional()) {
        FirstBlock = Br->getSuccessor(0);
      } else {
        errs() << "Entry terminator is not an unconditional branch!\n";
        return false;
      }
    } else {
      errs() << "Entry terminator is not a branch instruction!\n";
      return false;
    }

    // If entry block branches to a flattened block,
    // set initial state to that block's ID.
    uint32_t InitialState = 0;
    if (BlockToID.count(FirstBlock)) {
      InitialState = BlockToID[FirstBlock];
    } else {
      errs() << "Entry block does not branch to a flattened block!\n";
      return false;
    }

    EntryBuilder.SetInsertPoint(EntryTerminator);
    EntryBuilder.CreateStore(
        ConstantInt::get(EntryBuilder.getInt32Ty(), InitialState), StateVar);

    // Dispatcher logic.
    BasicBlock *Dispatcher = BasicBlock::Create(Ctx, "dispatcher", &F);
    BasicBlock *LoopEnd = BasicBlock::Create(Ctx, "loop_end", &F);
    IRBuilder<> LoopEndBuilder(LoopEnd);
    LoopEndBuilder.CreateBr(Dispatcher);
    IRBuilder<> DispatchBuilder(Dispatcher);
    LoadInst *StateLoad = DispatchBuilder.CreateLoad(
        DispatchBuilder.getInt32Ty(), StateVar, "state_val");
    SwitchInst *Switch =
        DispatchBuilder.CreateSwitch(StateLoad, LoopEnd, OrigBlocks.size());
    outs() << "Dispatcher:\n";
    for (BasicBlock *BB : OrigBlocks) {
      uint32_t ID = BlockToID[BB];
      Switch->addCase(ConstantInt::get(DispatchBuilder.getInt32Ty(), ID), BB);
      outs() << "  Block '" << BB->getName() << "' assigned ID: " << ID << "\n";
    }

    // Modify entry to jump to the dispatcher.
    EntryTerminator->eraseFromParent();
    IRBuilder<> EntryBuilder2(EntryBlock);
    EntryBuilder2.CreateBr(Dispatcher);

    // Replace all block terminators with state updates + loop back.
    // Loop back means jumping back to the dispatcher.
    for (BasicBlock *BB : OrigBlocks) {
      Instruction *Term = BB->getTerminator();

      if (!Term) {
        assert(Term && "Basic block has no terminator.");
      }

      // Unconditional branches.
      if (auto *Br = dyn_cast<BranchInst>(Term)) {
        if (Br->isUnconditional()) {
          BasicBlock *Succ = Br->getSuccessor(0);

          // Check if successor is one of the flattened blocks.
          if (!BlockToID.count(Succ))
            continue;

          IRBuilder<> B(Term);

          // Update state to the successor's ID.
          B.CreateStore(ConstantInt::get(B.getInt32Ty(), BlockToID[Succ]),
                        StateVar);

          // Replace branch with jump tp the dispatcher.
          B.CreateBr(Dispatcher);
          Term->eraseFromParent();
        } else {
          // Conditional branches.

          BasicBlock *TrueSucc = Br->getSuccessor(0);
          BasicBlock *FalseSucc = Br->getSuccessor(1);
          Value *Cond = Br->getCondition();

          IRBuilder<> B(Term);

          Value *TrueState = nullptr;
          Value *FalseState = nullptr;

          if (BlockToID.count(TrueSucc)) {
            TrueState = ConstantInt::get(B.getInt32Ty(), BlockToID[TrueSucc]);
          }

          if (BlockToID.count(FalseSucc)) {
            FalseState = ConstantInt::get(B.getInt32Ty(), BlockToID[FalseSucc]);
          }

          // Both successors are flattened.
          if (TrueState && FalseState) {
            Value *NewState = B.CreateSelect(Cond, TrueState, FalseState);
            B.CreateStore(NewState, StateVar);
            B.CreateBr(Dispatcher);
            Term->eraseFromParent();
          }

          // Only the true branch is flattened.
          else if (TrueState) {
            BasicBlock *UpdateBlock =
                BasicBlock::Create(Ctx, "update_true", &F);
            IRBuilder<> UB(UpdateBlock);
            UB.CreateStore(TrueState, StateVar);
            UB.CreateBr(Dispatcher);

            B.CreateCondBr(Cond, UpdateBlock, FalseSucc);
            Term->eraseFromParent();
          }

          // Only the false branch is flattened.
          else if (FalseState) {
            BasicBlock *UpdateBlock =
                BasicBlock::Create(Ctx, "update_false", &F);
            IRBuilder<> UB(UpdateBlock);
            UB.CreateStore(FalseState, StateVar);
            UB.CreateBr(Dispatcher);

            B.CreateCondBr(Cond, TrueSucc, UpdateBlock);
            Term->eraseFromParent();
          }
        }
      }
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "flatten-cfg") {
          MPM.addPass(FlattenCfgPass());
          return true;
        }

        if (Name.consume_front("flatten-cfg<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(FlattenCfgPass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FlattenCfgPass", LLVM_VERSION_STRING,
          registerPass};
}
