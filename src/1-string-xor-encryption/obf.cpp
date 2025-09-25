// LLVM pass that replaces C strings with XOR-encrypted versions and
// decrypts them at runtime.

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
#include "llvm/Support/raw_ostream.h"

#include <random>
#include <vector>

using namespace llvm;

namespace {

class StringEncryptionPass : public PassInfoMixin<StringEncryptionPass> {
public:
  StringEncryptionPass() : RNG(getRandomSeed()) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    std::vector<GlobalVariable *> StringGlobals = std::move(locateStrings(M));

    if (StringGlobals.empty()) {
      outs() << "StringEncryptionPass: Could not locate any strings\n";
      return PreservedAnalyses::all();
    }

    Function *DecryptFunc = createDecryptionFunction(M);

    Changed = encryptStrings(M, StringGlobals, DecryptFunc);

    if (Changed) {
      outs() << "StringEncryptionPass: Encrypted " << StringGlobals.size()
             << " strings\n";
    }

    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  std::mt19937 RNG;

  static unsigned getRandomSeed() {
    static std::random_device rd;
    return rd();
  }

  std::vector<GlobalVariable *> locateStrings(Module &M) {
    std::vector<GlobalVariable *> StringGlobals;

    for (GlobalVariable &GV : M.globals()) {
      if (GV.hasInitializer()) {
        if (ConstantDataArray *CDA =
                dyn_cast<ConstantDataArray>(GV.getInitializer())) {
          if (CDA->isCString()) {
            StringGlobals.push_back(&GV);
          }
        }
      }
    }
    return StringGlobals;
  }

  bool encryptStrings(Module &M,
                      const std::vector<GlobalVariable *> &StringGlobals,
                      Function *DecryptFunc) {
    LLVMContext &Ctx = M.getContext();
    for (GlobalVariable *OrigGV : StringGlobals) {

      // We already checked earlier if cast is safe to use.
      auto *CDA = cast<ConstantDataArray>(OrigGV->getInitializer());
      StringRef OrigStr = CDA->getAsString();

      uint8_t Key = RNG();

      std::vector<uint8_t> EncryptedData;
      for (size_t i = 0; i < OrigStr.size(); ++i) {
        uint8_t EncryptedByte = OrigStr[i] ^ Key;
        EncryptedData.push_back(EncryptedByte);
      }

      ArrayType *AT =
          ArrayType::get(Type::getInt8Ty(Ctx), EncryptedData.size());
      Constant *EncryptedArray = ConstantDataArray::get(Ctx, EncryptedData);

      std::string EncName = "__obf_str_" + std::to_string(RNG());
      GlobalVariable *EncGV = new GlobalVariable(
          M, AT, true, GlobalValue::PrivateLinkage, EncryptedArray, EncName);

      std::vector<Instruction *> UsesToReplace;
      for (User *U : OrigGV->users()) {
        if (Instruction *I = dyn_cast<Instruction>(U)) {
          UsesToReplace.push_back(I);
        }
      }

      for (Instruction *I : UsesToReplace) {
        IRBuilder<> Builder(I);

        Value *EncPtr =
            Builder.CreateBitCast(EncGV, PointerType::getUnqual(Ctx));
        Value *KeyVal = ConstantInt::get(Type::getInt8Ty(Ctx), Key);
        Value *LenVal = ConstantInt::get(Type::getInt64Ty(Ctx), OrigStr.size());

        Value *DecryptedStr =
            Builder.CreateCall(DecryptFunc, {EncPtr, KeyVal, LenVal});

        for (unsigned OpIdx = 0; OpIdx < I->getNumOperands(); ++OpIdx) {
          if (I->getOperand(OpIdx) == OrigGV) {
            I->setOperand(OpIdx, DecryptedStr);
          }
        }
      }
      if (OrigGV->use_empty()) {
        OrigGV->eraseFromParent();
      }
    }
    return true;
  }

  Function *getOrCreateMallocFunction(Module &M) {
    Function *MallocFunction = M.getFunction("malloc");
    if (MallocFunction) {
      return MallocFunction;
    }

    LLVMContext &Ctx = M.getContext();

    // Function signature: void* malloc(size_t size)
    FunctionType *FT = FunctionType::get(PointerType::getUnqual(Ctx),
                                         {Type::getInt64Ty(Ctx)}, false);

    Function *F = Function::Create(FT, Function::ExternalLinkage, "malloc", M);
    return F;
  }

  Function *createDecryptionFunction(Module &M) {
    LLVMContext &Ctx = M.getContext();

    // Function signature: i8* __obf_decrypt(i8* ptr, i8 key, i64 len)
    FunctionType *FT =
        FunctionType::get(PointerType::getUnqual(Ctx),
                          {PointerType::getUnqual(Ctx), Type::getInt8Ty(Ctx),
                           Type::getInt64Ty(Ctx)},
                          false);

    Function *F =
        Function::Create(FT, Function::PrivateLinkage, "__obf_decrypt", M);

    auto Args = F->args();
    auto ArgIter = Args.begin();
    ArgIter->setName("enc_ptr");
    Value *EncryptedPtr = &*ArgIter++;
    ArgIter->setName("key");
    Value *Key = &*ArgIter++;
    ArgIter->setName("len");
    Value *Len = &*ArgIter;

    Function *MallocFunction = getOrCreateMallocFunction(M);

    BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> Builder(Entry);

    Value *DecryptedPtr = Builder.CreateCall(MallocFunction, {Len}, "dec_ptr");

    BasicBlock *LoopHeader = BasicBlock::Create(Ctx, "loop_header", F);
    Builder.CreateBr(LoopHeader);
    Builder.SetInsertPoint(LoopHeader);
    PHINode *IndexPhi = Builder.CreatePHI(Type::getInt64Ty(Ctx), 2, "phi_idx");
    IndexPhi->addIncoming(ConstantInt::get(Type::getInt64Ty(Ctx), 0), Entry);

    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop_body", F);
    BasicBlock *LoopExit = BasicBlock::Create(Ctx, "loop_exit", F);
    Value *Condition = Builder.CreateICmpULT(IndexPhi, Len, "cond");
    Builder.CreateCondBr(Condition, LoopBody, LoopExit);

    Builder.SetInsertPoint(LoopBody);
    Value *SrcGEP =
        Builder.CreateGEP(Type::getInt8Ty(Ctx), EncryptedPtr, IndexPhi, "src_gep");
    Value *DstGEP =
        Builder.CreateGEP(Type::getInt8Ty(Ctx), DecryptedPtr, IndexPhi, "dst_gep");
    Value *EncryptedByte = Builder.CreateLoad(Type::getInt8Ty(Ctx), SrcGEP, "enc_byte");
    Value *DecryptedByte = Builder.CreateXor(EncryptedByte, Key, "dec_byte");
    Builder.CreateStore(DecryptedByte, DstGEP);

    Value *NextIndex =
        Builder.CreateAdd(IndexPhi, ConstantInt::get(Type::getInt64Ty(Ctx), 1), "next_idx");
    IndexPhi->addIncoming(NextIndex, LoopBody);
    Builder.CreateBr(LoopHeader);

    Builder.SetInsertPoint(LoopExit);
    Builder.CreateRet(DecryptedPtr);

    return F;
  }
};
} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "string-xor-encryption") {
          MPM.addPass(StringEncryptionPass());
          return true;
        }
        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "StringEncryptionPass", LLVM_VERSION_STRING,
          registerPass};
}