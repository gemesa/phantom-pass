/*
An LLVM pass that replaces C strings with RC4-encrypted versions and decrypts
them at runtime. The decrypted string is stored in the original encrypted global
variable. The pass automatically implements the decrypt function and calls it
before the string is used.

Known limitations:
- only [C
strings](https://llvm.org/doxygen/classllvm_1_1ConstantDataSequential.html#aecff3ad6cfa0e4abfd4fc9484d973e7d)
are supported at this time
- the decrypted strings are not re-encrypted after use, meaning they stay
unencrypted in the memory
- the RC4 key (`"MySecretKey"`) is hardcoded into the binary
- increased code size
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
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <openssl/rc4.h>

#include <random>
#include <vector>

using namespace llvm;

namespace {

class StringEncryptionPass : public PassInfoMixin<StringEncryptionPass> {
public:
  StringEncryptionPass() : RNG(getRandomSeed()) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    std::vector<GlobalVariable *> StringGlobals = locateStrings(M);

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
        if (auto *CDA = dyn_cast<ConstantDataArray>(GV.getInitializer())) {
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

      SmallVector<uint8_t> Data(OrigStr.begin(), OrigStr.end());
      SmallString<64> Key("MySecretKey");

      RC4_KEY rc4_key;

      RC4_set_key(&rc4_key, Key.size(),
                  reinterpret_cast<const uint8_t *>(Key.data()));
      RC4(&rc4_key, Data.size(), Data.data(), Data.data());

      ArrayType *AT = ArrayType::get(Type::getInt8Ty(Ctx), Data.size());
      Constant *EncryptedArray = ConstantDataArray::get(Ctx, Data);

      SmallString<64> EncName = formatv("__obf_str_{0}", RNG());
      GlobalVariable *EncGV = new GlobalVariable(
          M, AT, false, GlobalValue::PrivateLinkage, EncryptedArray, EncName);

      std::vector<Instruction *> UsesToReplace;
      for (User *U : OrigGV->users()) {
        if (auto *I = dyn_cast<Instruction>(U)) {
          UsesToReplace.push_back(I);
        }
      }

      ArrayType *KeyType = ArrayType::get(Type::getInt8Ty(Ctx), Key.size());
      Constant *KeyArray = ConstantDataArray::get(
          Ctx, ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(Key.data()),
                                 Key.size()));
      GlobalVariable *KeyGV = new GlobalVariable(
          M, KeyType, true, GlobalValue::PrivateLinkage, KeyArray, "key");

      for (Instruction *I : UsesToReplace) {
        IRBuilder<> Builder(I);

        Value *DataPtr = Builder.CreateBitCast(EncGV, Builder.getPtrTy());

        Value *DataLen = Builder.getInt32(AT->getNumElements());

        Value *KeyPtr = Builder.CreateBitCast(KeyGV, Builder.getPtrTy());

        Value *KeyLen = Builder.getInt32(KeyType->getNumElements());

        Builder.CreateCall(DecryptFunc, {KeyPtr, KeyLen, DataPtr, DataLen});

        for (unsigned OpIdx = 0; OpIdx < I->getNumOperands(); ++OpIdx) {
          if (I->getOperand(OpIdx) == OrigGV) {
            I->setOperand(OpIdx, DataPtr);
          }
        }
      }
      if (OrigGV->use_empty()) {
        OrigGV->eraseFromParent();
      }
    }
    return true;
  }

  Function *createDecryptionFunction(Module &M) {
    /*
    void rc4(unsigned char *key, int keylen, unsigned char *data, int datalen) {
        unsigned char S[256];
        int i, j = 0, t;

        // KSA (Key Scheduling Algorithm)
        for (i = 0; i < 256; i++)
            S[i] = i;

        for (i = 0; i < 256; i++) {
            j = (j + S[i] + key[i % keylen]) % 256;
            t = S[i]; S[i] = S[j]; S[j] = t;
        }

        // PRGA (Pseudo-Random Generation Algorithm)
        i = j = 0;
        for (int k = 0; k < datalen; k++) {
            i = (i + 1) % 256;
            j = (j + S[i]) % 256;
            t = S[i]; S[i] = S[j]; S[j] = t;
            data[k] ^= S[(S[i] + S[j]) % 256];
        }
    }
    */
    LLVMContext &Ctx = M.getContext();

    // Function signature: void rc4(unsigned char *key, int keylen, unsigned
    // char *data, int datalen)
    FunctionType *FT =
        FunctionType::get(Type::getVoidTy(Ctx),
                          {PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx),
                           PointerType::getUnqual(Ctx), Type::getInt32Ty(Ctx)},
                          false);

    Function *F =
        Function::Create(FT, Function::PrivateLinkage, "__obf_decrypt", M);

    auto ArgIter = F->arg_begin();
    ArgIter->setName("key_ptr");
    Value *KeyPtr = &*ArgIter;
    ++ArgIter;
    ArgIter->setName("key_len");
    Value *KeyLen = &*ArgIter;
    ++ArgIter;
    ArgIter->setName("data_ptr");
    Value *DataPtr = &*ArgIter;
    ++ArgIter;
    ArgIter->setName("data_len");
    Value *DataLen = &*ArgIter;

    BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> Builder(Entry);

    /*
    unsigned char S[256];
    */

    ArrayType *SBoxType = ArrayType::get(Builder.getInt8Ty(), 256);
    Value *SBox = Builder.CreateAlloca(SBoxType, nullptr, "sbox");

    /*
    int j = 0, t;
    in this statement:
    int i, j = 0, t;
    */

    Value *J = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "j");
    Builder.CreateStore(Builder.getInt32(0), J);
    Value *T = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "t");

    /*
    int i = 0;
    in this statement:
    int i, j = 0, t;
    and
    for (i = 0; i < 256;
    in this loop header:
    for (i = 0; i < 256; i++)
    */

    BasicBlock *LoopHeader = BasicBlock::Create(Ctx, "loop_header", F);
    Builder.CreateBr(LoopHeader);
    Builder.SetInsertPoint(LoopHeader);

    PHINode *IndexPHI = Builder.CreatePHI(Builder.getInt32Ty(), 2, "index_phi");
    IndexPHI->addIncoming(Builder.getInt32(0), Entry);

    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop_body", F);
    BasicBlock *LoopExit = BasicBlock::Create(Ctx, "loop_exit", F);

    Value *Cond =
        Builder.CreateICmpULT(IndexPHI, Builder.getInt32(256), "cond");
    Builder.CreateCondBr(Cond, LoopBody, LoopExit);

    /*
    S[i] = i;
    */

    Builder.SetInsertPoint(LoopBody);
    Value *SBoxGEP = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), IndexPHI}, "sbox_gep");
    Value *IndexTrunc =
        Builder.CreateTrunc(IndexPHI, Builder.getInt8Ty(), "index_trunc");
    Builder.CreateStore(IndexTrunc, SBoxGEP);

    /*
    i++
    in this loop header:
    for (i = 0; i < 256; i++)
    */
    Value *NextIndexPHI =
        Builder.CreateAdd(IndexPHI, Builder.getInt32(1), "next_index_phi");
    IndexPHI->addIncoming(NextIndexPHI, LoopBody);

    /*
    continue
    in this loop:
    for (i = 0; i < 256; i++)
    */
    Builder.CreateBr(LoopHeader);

    Builder.SetInsertPoint(LoopExit);

    // Jump to the next for loop.
    BasicBlock *LoopHeader2 = BasicBlock::Create(Ctx, "loop_header2", F);
    Builder.CreateBr(LoopHeader2);
    Builder.SetInsertPoint(LoopHeader2);

    /*
    for (i = 0; i < 256;
    in this loop header:
    for (i = 0; i < 256; i++) {
    */

    PHINode *IndexPHI2 =
        Builder.CreatePHI(Builder.getInt32Ty(), 2, "index_phi2");
    IndexPHI2->addIncoming(Builder.getInt32(0), LoopExit);

    BasicBlock *LoopBody2 = BasicBlock::Create(Ctx, "loop_body2", F);
    BasicBlock *LoopExit2 = BasicBlock::Create(Ctx, "loop_exit2", F);

    Value *Cond2 =
        Builder.CreateICmpULT(IndexPHI2, Builder.getInt32(256), "cond2");
    Builder.CreateCondBr(Cond2, LoopBody2, LoopExit2);

    /*
    j
    in this statement:
    j = (j + S[i] + key[i % keylen]) % 256;
    */
    Builder.SetInsertPoint(LoopBody2);
    Value *JLoaded = Builder.CreateLoad(Builder.getInt32Ty(), J, "j_loaded");

    /*
    S[i]
    in this statement:
    j = (j + S[i] + key[i % keylen]) % 256;
    */
    Value *SBoxGEPI2 = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), IndexPHI2}, "sbox_gep_i2");
    Value *SBoxI2Loaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPI2, "s_i2_loaded");
    Value *SBoxI2LoadedExt =
        Builder.CreateZExt(SBoxI2Loaded, Builder.getInt32Ty(), "s_i2_ext");

    /*
    key[i % keylen]
    in this statement:
    j = (j + S[i] + key[i % keylen]) % 256;
    */
    Value *Mod0 = Builder.CreateSRem(IndexPHI2, KeyLen, "mod0");
    Value *KeyGEP =
        Builder.CreateGEP(Builder.getInt8Ty(), KeyPtr, Mod0, "key_gep");
    Value *KeyLoaded =
        Builder.CreateLoad(Builder.getInt8Ty(), KeyGEP, "key_loaded");

    /*
    additions
    in this statement:
    j = (j + S[i] + key[i % keylen]) % 256;
    */
    Value *Sum0 = Builder.CreateAdd(JLoaded, SBoxI2LoadedExt, "sum0");
    Value *KeyLoadedExt =
        Builder.CreateSExt(KeyLoaded, Builder.getInt32Ty(), "key_loaded_ext");
    Value *Sum1 = Builder.CreateAdd(Sum0, KeyLoadedExt, "sum1");

    /*
    mod and store
    in this statement:
    j = (j + S[i] + key[i % keylen]) % 256;
    */
    Value *Mod1 = Builder.CreateSRem(Sum1, Builder.getInt32(256), "mod1");
    Builder.CreateStore(Mod1, J);

    /*
    t = S[i];
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    Builder.CreateStore(SBoxI2LoadedExt, T);

    /*
    S[i] = S[j];
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    JLoaded = Builder.CreateLoad(Builder.getInt32Ty(), J, "j_loaded");
    Value *SBoxGEPJ = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), JLoaded}, "sbox_gep_j");
    Value *SBoxJLoaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPJ, "sbox_j_loaded");
    Builder.CreateStore(SBoxJLoaded, SBoxGEPI2);

    /*
    S[j] = t;
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    Value *TLoaded = Builder.CreateLoad(Builder.getInt32Ty(), T, "t_loaded");
    Value *TLoadedTrunc =
        Builder.CreateTrunc(TLoaded, Builder.getInt8Ty(), "t_trunc");
    Builder.CreateStore(TLoadedTrunc, SBoxGEPJ);

    /*
    i++
    in this loop header:
    for (i = 0; i < 256; i++) {
    */
    Value *NextIndexPHI2 =
        Builder.CreateAdd(IndexPHI2, Builder.getInt32(1), "next_index_phi2");
    IndexPHI2->addIncoming(NextIndexPHI2, LoopBody2);

    /*
    continue
    in this loop:
    for (i = 0; i < 256; i++) {
    */
    Builder.CreateBr(LoopHeader2);

    Builder.SetInsertPoint(LoopExit2);

    // Jump to the next for loop.
    /*
    i = j = 0;
    */
    Value *I3 = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "i3");
    Builder.CreateStore(Builder.getInt32(0), I3);
    Value *J3 = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "j3");
    Builder.CreateStore(Builder.getInt32(0), J3);

    BasicBlock *LoopHeader3 = BasicBlock::Create(Ctx, "loop_header3", F);
    Builder.CreateBr(LoopHeader3);
    Builder.SetInsertPoint(LoopHeader3);

    /*
    for (int k = 0; k < datalen;
    in this loop header:
    for (int k = 0; k < datalen; k++) {
    */

    PHINode *KPHI3 = Builder.CreatePHI(Builder.getInt32Ty(), 2, "k_phi3");
    KPHI3->addIncoming(Builder.getInt32(0), LoopExit2);

    BasicBlock *LoopBody3 = BasicBlock::Create(Ctx, "loop_body3", F);
    BasicBlock *LoopExit3 = BasicBlock::Create(Ctx, "loop_exit3", F);

    Value *Cond3 = Builder.CreateICmpULT(KPHI3, DataLen, "cond3");
    Builder.CreateCondBr(Cond3, LoopBody3, LoopExit3);

    Builder.SetInsertPoint(LoopBody3);

    /*
    i = (i + 1) % 256;
    */
    Value *I3Loaded = Builder.CreateLoad(Builder.getInt32Ty(), I3, "i3_loaded");
    Value *I3Inc = Builder.CreateAdd(I3Loaded, Builder.getInt32(1), "i3_inc");
    Value *Mod2 = Builder.CreateSRem(I3Inc, Builder.getInt32(256), "mod2");
    Builder.CreateStore(Mod2, I3);

    /*
    j = (j + S[i]) % 256;
    */
    Value *J3Loaded = Builder.CreateLoad(Builder.getInt32Ty(), J3, "j3_loaded");
    I3Loaded = Builder.CreateLoad(Builder.getInt32Ty(), I3, "i3_loaded");
    Value *SBoxGEPI3 = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), I3Loaded}, "sbox_gep_i3");
    Value *SBoxI3Loaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPI3, "sbox_i3_loaded");
    Value *SBoxI3LoadedExt =
        Builder.CreateZExt(SBoxI3Loaded, Builder.getInt32Ty(), "sbox_i3_ext");
    Value *Sum2 = Builder.CreateAdd(J3Loaded, SBoxI3LoadedExt, "sum2");
    Value *Mod3 = Builder.CreateSRem(Sum2, Builder.getInt32(256), "mod3");
    Builder.CreateStore(Mod3, J3);

    /*
    t = S[i];
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    Builder.CreateStore(SBoxI3LoadedExt, T);

    /*
    S[i] = S[j];
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    J3Loaded = Builder.CreateLoad(Builder.getInt32Ty(), J3, "j3_loaded");
    Value *SBoxGEPJ3 = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), J3Loaded}, "sbox_gep_j3");
    Value *SBoxJ3Loaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPJ3, "sbox_j3_loaded");
    Builder.CreateStore(SBoxJ3Loaded, SBoxGEPI3);

    /*
    S[j] = t;
    in this statement:
    t = S[i]; S[i] = S[j]; S[j] = t;
    */
    TLoaded = Builder.CreateLoad(Builder.getInt32Ty(), T, "t_loaded");
    Value *TLoadedTrunc2 =
        Builder.CreateTrunc(TLoaded, Builder.getInt8Ty(), "t_trunc2");
    Builder.CreateStore(TLoadedTrunc2, SBoxGEPJ3);

    /*
    data[k] ^= S[(S[i] + S[j]) % 256];
    */
    SBoxI3Loaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPI3, "sbox_i3_loaded");
    Value *SBoxI3LoadedExt2 =
        Builder.CreateZExt(SBoxI3Loaded, Builder.getInt32Ty(), "sbox_i3_ext2");
    SBoxJ3Loaded =
        Builder.CreateLoad(Builder.getInt8Ty(), SBoxGEPJ3, "sbox_j3_loaded");
    Value *SBoxJ3LoadedExt =
        Builder.CreateZExt(SBoxJ3Loaded, Builder.getInt32Ty(), "sbox_j3_ext");
    Value *Sum3 = Builder.CreateAdd(SBoxI3LoadedExt2, SBoxJ3LoadedExt, "sum3");
    Value *Mod4 = Builder.CreateSRem(Sum3, Builder.getInt32(256), "mod4");
    Value *SBoxGEPMod4 = Builder.CreateInBoundsGEP(
        SBoxType, SBox, {Builder.getInt32(0), Mod4}, "sbox_gep_mod4");
    Value *SBoxGEPMod4Loaded = Builder.CreateLoad(
        Builder.getInt8Ty(), SBoxGEPMod4, "sbox_gep_mod4_loaded");
    Value *DataGEP =
        Builder.CreateGEP(Builder.getInt8Ty(), DataPtr, KPHI3, "data_gep");
    Value *DataLoaded =
        Builder.CreateLoad(Builder.getInt8Ty(), DataGEP, "data_loaded");
    Value *Xor = Builder.CreateXor(DataLoaded, SBoxGEPMod4Loaded, "xor");
    Builder.CreateStore(Xor, DataGEP);

    /*
    k++
    in this loop header:
    ffor (int k = 0; k < datalen; k++) {
    */
    Value *NextKPHI3 =
        Builder.CreateAdd(KPHI3, Builder.getInt32(1), "next_k_phi3");
    KPHI3->addIncoming(NextKPHI3, LoopBody3);

    /*
    continue
    in this loop:
    for (int k = 0; k < datalen; k++) {
    */
    Builder.CreateBr(LoopHeader3);

    Builder.SetInsertPoint(LoopExit3);
    Builder.CreateRetVoid();

    return F;
  }
};
} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "string-rc4-encryption") {
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