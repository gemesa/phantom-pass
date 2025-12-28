/*
The documentation is available here:
https://shadowshell.io/phantom-pass/18-virtual-machine-instruction.html
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

enum VMOpcode : uint8_t {
  VM_ADD = 0x01,
  VM_SUB = 0x02,
  VM_MUL = 0x03,
  VM_AND = 0x04,
  VM_OR = 0x05,
  VM_XOR = 0x06,
  VM_SHL = 0x07,
  VM_SHR = 0x08,
};

class VirtualMachinePass : public PassInfoMixin<VirtualMachinePass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<RandomNumberGenerator> RNG;
  Function *VMDispatcher = nullptr;

public:
  VirtualMachinePass() = default;

  VirtualMachinePass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    RNG = M.createRNG("virtual-machine");
    VMDispatcher = getOrCreateVMDispatcher(M);

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (F.isDeclaration() || F.isIntrinsic()) {
        continue;
      }

      if (virtualizeInstructions(F, M)) {
        Changed = true;
        outs() << "VirtualMachinePass: instructions replaced in function '"
               << F.getName() << "'\n";
      }
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  Function *getOrCreateVMDispatcher(Module &M) {
    LLVMContext &Ctx = M.getContext();

    if (Function *F = M.getFunction("__vm_dispatch")) {
      return F;
    }

    IntegerType *I8Ty = Type::getInt8Ty(Ctx);
    IntegerType *I64Ty = Type::getInt64Ty(Ctx);

    // int64_t __vm_dispatch(uint8_t op, int64_t a, int64_t b);
    FunctionType *FTy = FunctionType::get(I64Ty, {I8Ty, I64Ty, I64Ty}, false);

    Function *F =
        Function::Create(FTy, Function::PrivateLinkage, "__vm_dispatch", M);

    // The dispatcher should not be inlined or optimized away.
    // That would defeat the point of the virtualization.
    F->addFnAttr(Attribute::NoInline);
    F->addFnAttr(Attribute::OptimizeNone);

    Argument *OpArg = F->getArg(0);
    Argument *AArg = F->getArg(1);
    Argument *BArg = F->getArg(2);
    OpArg->setName("op");
    AArg->setName("a");
    BArg->setName("b");

    BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", F);
    BasicBlock *AddBB = BasicBlock::Create(Ctx, "add", F);
    BasicBlock *SubBB = BasicBlock::Create(Ctx, "sub", F);
    BasicBlock *MulBB = BasicBlock::Create(Ctx, "mul", F);
    BasicBlock *AndBB = BasicBlock::Create(Ctx, "and", F);
    BasicBlock *OrBB = BasicBlock::Create(Ctx, "or", F);
    BasicBlock *XorBB = BasicBlock::Create(Ctx, "xor", F);
    BasicBlock *ShlBB = BasicBlock::Create(Ctx, "shl", F);
    BasicBlock *ShrBB = BasicBlock::Create(Ctx, "shr", F);
    BasicBlock *DefaultBB = BasicBlock::Create(Ctx, "default", F);

    IRBuilder<> Builder(Ctx);
    Builder.SetInsertPoint(EntryBB);
    SwitchInst *Switch = Builder.CreateSwitch(OpArg, DefaultBB, 8);
    Switch->addCase(ConstantInt::get(I8Ty, VM_ADD), AddBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_SUB), SubBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_MUL), MulBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_AND), AndBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_OR), OrBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_XOR), XorBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_SHL), ShlBB);
    Switch->addCase(ConstantInt::get(I8Ty, VM_SHR), ShrBB);

    auto EmitBinaryOp = [&](BasicBlock *BB, Instruction::BinaryOps BinOp,
                            const char *Name) {
      Builder.SetInsertPoint(BB);
      Value *Result = Builder.CreateBinOp(BinOp, AArg, BArg, Name);
      Builder.CreateRet(Result);
    };

    EmitBinaryOp(AddBB, Instruction::Add, "add_res");
    EmitBinaryOp(MulBB, Instruction::Mul, "add_res");
    EmitBinaryOp(SubBB, Instruction::Sub, "add_res");
    EmitBinaryOp(AndBB, Instruction::And, "add_res");
    EmitBinaryOp(OrBB, Instruction::Or, "add_res");
    EmitBinaryOp(XorBB, Instruction::Xor, "add_res");
    EmitBinaryOp(ShlBB, Instruction::Shl, "add_res");
    EmitBinaryOp(ShrBB, Instruction::LShr, "add_res");

    Builder.SetInsertPoint(DefaultBB);
    Builder.CreateRet(ConstantInt::get(I64Ty, 0));

    return F;
  }

  std::optional<uint8_t> getVMOpcode(Instruction::BinaryOps LLVMOpcode) {
    switch (LLVMOpcode) {
    case Instruction::Add:
      return VM_ADD;
    case Instruction::Sub:
      return VM_SUB;
    case Instruction::Mul:
      return VM_MUL;
    case Instruction::And:
      return VM_AND;
    case Instruction::Or:
      return VM_OR;
    case Instruction::Xor:
      return VM_XOR;
    case Instruction::Shl:
      return VM_SHL;
    case Instruction::LShr:
      return VM_SHR;

    default:
      return std::nullopt;
    }
  }

  bool virtualizeInstructions(Function &F, Module &M) {
    LLVMContext &Ctx = M.getContext();
    Type *I8Ty = Type::getInt8Ty(Ctx);
    Type *I64Ty = Type::getInt64Ty(Ctx);

    SmallVector<Instruction *, 32> ToVirtualize;

    for (BasicBlock &BB : F) {
      for (Instruction &I : BB) {
        if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
          // Only integer type is supported.
          if (!BO->getType()->isIntegerTy()) {
            continue;
          }
          // Skip unsupported operations.
          if (!getVMOpcode(BO->getOpcode()).has_value()) {
            continue;
          }
          ToVirtualize.push_back(&I);
        }
      }
    }

    if (ToVirtualize.empty()) {
      errs()
          << "VirtualMachinePass: no instructions to virtualize in function '"
          << F.getName() << "'";
      return false;
    }

    for (Instruction *I : ToVirtualize) {
      auto *BO = cast<BinaryOperator>(I);
      IRBuilder<> Builder(BO);

      uint8_t Opcode = getVMOpcode(BO->getOpcode()).value();
      Type *OrigTy = BO->getType();

      Value *A = BO->getOperand(0);
      Value *B = BO->getOperand(1);

      Value *AExt = Builder.CreateSExt(A, I64Ty, "a_ext");
      Value *BExt = Builder.CreateSExt(B, I64Ty, "b_ext");

      Value *OpcodeVal = ConstantInt::get(I8Ty, Opcode);

      Value *Result = Builder.CreateCall(VMDispatcher, {OpcodeVal, AExt, BExt},
                                         "vm_result");

      Value *ResultTrunc = Builder.CreateTrunc(Result, OrigTy, "vm_trunc");

      BO->replaceAllUsesWith(ResultTrunc);
      BO->eraseFromParent();
    }

    return true;
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "virtual-machine") {
          MPM.addPass(VirtualMachinePass());
          return true;
        }

        if (Name.consume_front("virtual-machine<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);

            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());

            MPM.addPass(VirtualMachinePass(Functions));
            return true;
          }
        }

        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "VirtualMachinePass", LLVM_VERSION_STRING,
          registerPass};
}
