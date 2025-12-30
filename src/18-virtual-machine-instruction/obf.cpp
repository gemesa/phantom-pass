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
  Function *VMDispatcher = nullptr;
  GlobalVariable *RegisterFile = nullptr;
  unsigned BytecodeCounter = 0;

  static constexpr uint8_t REG_SRC0 = 0;
  static constexpr uint8_t REG_SRC1 = 1;
  static constexpr uint8_t REG_DST = 2;

public:
  VirtualMachinePass() = default;

  VirtualMachinePass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    BytecodeCounter = 0;
    RegisterFile = getOrCreateRegisterFile(M);
    VMDispatcher = getOrCreateVMDispatcher(M);

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }

      if (&F == VMDispatcher) {
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
  GlobalVariable *createBytecode(Module &M, uint8_t Opcode, uint8_t Dst,
                                 uint8_t Src0, uint8_t Src1) {
    LLVMContext &Ctx = M.getContext();
    IntegerType *I8Ty = Type::getInt8Ty(Ctx);
    ArrayType *BytecodeTy = ArrayType::get(I8Ty, 4);

    std::vector<Constant *> Bytes = {
        ConstantInt::get(I8Ty, Opcode),
        ConstantInt::get(I8Ty, Dst),
        ConstantInt::get(I8Ty, Src0),
        ConstantInt::get(I8Ty, Src1),
    };

    Constant *Init = ConstantArray::get(BytecodeTy, Bytes);

    std::string Name = "__vm_bc_" + std::to_string(BytecodeCounter++);

    return new GlobalVariable(M, BytecodeTy, true, GlobalValue::PrivateLinkage,
                              Init, Name);
  }

  GlobalVariable *getOrCreateRegisterFile(Module &M) {
    if (GlobalVariable *GV = M.getGlobalVariable("__vm_regs")) {
      return GV;
    }

    LLVMContext &Ctx = M.getContext();
    Type *I64Ty = Type::getInt64Ty(Ctx);
    ArrayType *RegFileType = ArrayType::get(I64Ty, 256);

    return new GlobalVariable(M, RegFileType, false,
                              GlobalValue::PrivateLinkage,
                              Constant::getNullValue(RegFileType), "__vm_regs");
  }

  Function *getOrCreateVMDispatcher(Module &M) {
    LLVMContext &Ctx = M.getContext();

    if (Function *F = M.getFunction("__vm_exec")) {
      return F;
    }

    IntegerType *I8Ty = Type::getInt8Ty(Ctx);
    IntegerType *I64Ty = Type::getInt64Ty(Ctx);
    Type *VoidTy = Type::getVoidTy(Ctx);
    PointerType *I8PtrTy = PointerType::getUnqual(I8Ty);

    // void __vm_exec(int8_t* bytecode);
    FunctionType *FTy = FunctionType::get(VoidTy, {I8PtrTy}, false);

    Function *F =
        Function::Create(FTy, Function::PrivateLinkage, "__vm_exec", M);

    // The dispatcher should not be inlined or optimized away.
    // That would defeat the point of the virtualization.
    F->addFnAttr(Attribute::NoInline);
    F->addFnAttr(Attribute::OptimizeNone);

    Argument *BytecodeArg = F->getArg(0);
    BytecodeArg->setName("bytecode");

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

    // bytecode: [op, dst, src0,  src1]
    Value *OpPtr = Builder.CreateInBoundsGEP(I8Ty, BytecodeArg,
                                             Builder.getInt64(0), "op_ptr");
    Value *DstPtr = Builder.CreateInBoundsGEP(I8Ty, BytecodeArg,
                                              Builder.getInt64(1), "dst_ptr");
    Value *Src0Ptr = Builder.CreateInBoundsGEP(I8Ty, BytecodeArg,
                                               Builder.getInt64(2), "src0_ptr");
    Value *Src1Ptr = Builder.CreateInBoundsGEP(I8Ty, BytecodeArg,
                                               Builder.getInt64(3), "src1_ptr");

    Value *Op = Builder.CreateLoad(I8Ty, OpPtr, "op");
    Value *Dst = Builder.CreateLoad(I8Ty, DstPtr, "dst");
    Value *Src0 = Builder.CreateLoad(I8Ty, Src0Ptr, "src0");
    Value *Src1 = Builder.CreateLoad(I8Ty, Src1Ptr, "src1");

    Value *Src0Ext = Builder.CreateZExt(Src0, I64Ty, "src0_ext");
    Value *Src1Ext = Builder.CreateZExt(Src1, I64Ty, "src1_ext");

    Value *Src0RegPtr = Builder.CreateInBoundsGEP(
        RegisterFile->getValueType(), RegisterFile,
        {Builder.getInt64(0), Src0Ext}, "src0_reg_ptr");
    Value *Src1RegPtr = Builder.CreateInBoundsGEP(
        RegisterFile->getValueType(), RegisterFile,
        {Builder.getInt64(0), Src1Ext}, "src1_reg_ptr");

    Value *A = Builder.CreateLoad(I64Ty, Src0RegPtr, "a");
    Value *B = Builder.CreateLoad(I64Ty, Src1RegPtr, "b");

    SwitchInst *Switch = Builder.CreateSwitch(Op, DefaultBB, 8);
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
      Value *Result = Builder.CreateBinOp(BinOp, A, B, Name);
      Value *DstExt = Builder.CreateZExt(Dst, I64Ty, "dst_ext");
      Value *DstPtr =
          Builder.CreateInBoundsGEP(RegisterFile->getValueType(), RegisterFile,
                                    {Builder.getInt64(0), DstExt}, "dst_ptr");
      Builder.CreateStore(Result, DstPtr);

      Builder.CreateRetVoid();
    };

    EmitBinaryOp(AddBB, Instruction::Add, "add_res");
    EmitBinaryOp(MulBB, Instruction::Mul, "mul_res");
    EmitBinaryOp(SubBB, Instruction::Sub, "sub_res");
    EmitBinaryOp(AndBB, Instruction::And, "and_res");
    EmitBinaryOp(OrBB, Instruction::Or, "or_res");
    EmitBinaryOp(XorBB, Instruction::Xor, "xor_res");
    EmitBinaryOp(ShlBB, Instruction::Shl, "shl_res");
    EmitBinaryOp(ShrBB, Instruction::LShr, "lshr_res");

    Builder.SetInsertPoint(DefaultBB);
    Builder.CreateRetVoid();

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

      Value *Src0Ptr = Builder.CreateInBoundsGEP(
          RegisterFile->getValueType(), RegisterFile,
          {Builder.getInt64(0), Builder.getInt64(REG_SRC0)}, "src0_ptr");
      Value *Src1Ptr = Builder.CreateInBoundsGEP(
          RegisterFile->getValueType(), RegisterFile,
          {Builder.getInt64(0), Builder.getInt64(REG_SRC1)}, "src1_ptr");

      Builder.CreateStore(AExt, Src0Ptr);
      Builder.CreateStore(BExt, Src1Ptr);

      GlobalVariable *Bytecode =
          createBytecode(M, Opcode, REG_DST, REG_SRC0, REG_SRC1);

      Value *BytecodePtr = Builder.CreateInBoundsGEP(
          Bytecode->getValueType(), Bytecode,
          {Builder.getInt64(0), Builder.getInt64(0)}, "bytecode_ptr");

      Builder.CreateCall(VMDispatcher, {BytecodePtr});

      Value *DstPtr = Builder.CreateInBoundsGEP(
          RegisterFile->getValueType(), RegisterFile,
          {Builder.getInt64(0), Builder.getInt64(REG_DST)}, "dst_ptr");

      Value *Result = Builder.CreateLoad(I64Ty, DstPtr, "vm_result");

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
