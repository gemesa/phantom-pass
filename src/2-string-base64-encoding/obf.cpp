/*
The documentation is available here:
https://shadowshell.io/phantom-pass/2-string-base64-encoding.html
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

#include <boost/beast/core/detail/base64.hpp>

#include <random>
#include <vector>

using namespace llvm;

namespace {

class StringBase64EncodePass : public PassInfoMixin<StringBase64EncodePass> {
public:
  StringBase64EncodePass() : RNG(getRandomSeed()) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    SmallVector<GlobalVariable *, 128> StringGlobals = locateStrings(M);

    if (StringGlobals.empty()) {
      outs() << "StringBase64EncodePass: Could not locate any strings\n";
      return PreservedAnalyses::all();
    }

    Function *DecodeFunction = createBase64DecodeFunction(M);

    Changed = encodeStrings(M, StringGlobals, DecodeFunction);

    if (Changed) {
      outs() << "StringBase64EncodePass: Encoded " << StringGlobals.size()
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

  SmallVector<GlobalVariable *, 128> locateStrings(Module &M) {
    SmallVector<GlobalVariable *, 128> StringGlobals;

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

  std::string base64Encode(StringRef input) {
    std::string output;
    output.resize(boost::beast::detail::base64::encoded_size(input.size()));
    auto written = boost::beast::detail::base64::encode(
        &output[0], input.data(), input.size());
    output.resize(written);
    return output;
  }

  bool encodeStrings(Module &M,
                     const SmallVector<GlobalVariable *, 128> &StringGlobals,
                     Function *DecodeFunction) {
    LLVMContext &Ctx = M.getContext();
    for (GlobalVariable *OrigGV : StringGlobals) {

      auto *CDA = cast<ConstantDataArray>(OrigGV->getInitializer());
      StringRef OrigStr = CDA->getAsString();

      std::string encoded = base64Encode(OrigStr);

      Constant *EncodedArray = ConstantDataArray::getString(Ctx, encoded, true);

      SmallString<64> EncName = formatv("__obf_str_{0}", RNG());
      GlobalVariable *EncGV = new GlobalVariable(
          M, EncodedArray->getType(), false, GlobalValue::PrivateLinkage,
          EncodedArray, EncName);

      SmallVector<Instruction *, 16> UsesToReplace;
      for (User *U : OrigGV->users()) {
        if (auto *I = dyn_cast<Instruction>(U)) {
          UsesToReplace.push_back(I);
        }
      }

      for (Instruction *I : UsesToReplace) {
        IRBuilder<> Builder(I);

        Value *EncPtr = Builder.CreateBitCast(EncGV, Builder.getPtrTy());
        Value *EncodedLen = Builder.getInt64(encoded.size());

        Builder.CreateCall(DecodeFunction, {EncPtr, EncodedLen});

        for (unsigned OpIdx = 0; OpIdx < I->getNumOperands(); ++OpIdx) {
          if (I->getOperand(OpIdx) == OrigGV) {
            I->setOperand(OpIdx, EncPtr);
          }
        }
      }

      if (OrigGV->use_empty()) {
        OrigGV->eraseFromParent();
      }
    }
    return true;
  }

  Function *createBase64DecodeFunction(Module &M) {

    /*
    We implement the following global variable and function:

    static const int T[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    // Where: '+' (43) = 62, '/' (47) = 63
    //        '0'-'9' (48-57) = 52-61
    //        'A'-'Z' (65-90) = 0-25
    //        'a'-'z' (97-122) = 26-51

    void base64_decode_inplace(char* input, size_t length) {
        int val = 0, bits = -8;
        size_t out_pos = 0;

        for (size_t i = 0; i < length; i++) {
            unsigned char c = input[i];
            //if (T[c] == -1) break;
            val = (val << 6) + T[c];
            bits += 6;
            if (bits >= 0) {
                input[out_pos++] = (char)((val >> bits) & 0xFF);
                bits -= 8;
            }
        }
    }
    */

    LLVMContext &Ctx = M.getContext();

    // Function signature: void __obf_base64_decode(i8* ptr, i64 len)
    FunctionType *FT = FunctionType::get(
        Type::getVoidTy(Ctx),
        {PointerType::getUnqual(Ctx), Type::getInt64Ty(Ctx)}, false);

    Function *F = Function::Create(FT, Function::PrivateLinkage,
                                   "__obf_base64_decode", M);

    auto ArgIter = F->arg_begin();
    ArgIter->setName("enc_ptr");
    Value *EncodedPtr = &*ArgIter;
    ArgIter++;
    ArgIter->setName("len");
    Value *Len = &*ArgIter;

    BasicBlock *Entry = BasicBlock::Create(Ctx, "entry", F);
    IRBuilder<> Builder(Entry);

    /*
    static const int T[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    // Where: '+' (43) = 62, '/' (47) = 63
    //        '0'-'9' (48-57) = 52-61
    //        'A'-'Z' (65-90) = 0-25
    //        'a'-'z' (97-122) = 26-51
    */

    std::vector<Constant *> TableValues(256, Builder.getInt32(-1));

    SmallString<64> chars(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");
    for (int i = 0; i < 64; i++) {
      TableValues[(unsigned char)chars[i]] = Builder.getInt32(i);
    }

    ArrayType *TableType = ArrayType::get(Builder.getInt32Ty(), 256);

    Constant *TableInit = ConstantArray::get(TableType, TableValues);

    // T is __obf_char_table
    GlobalVariable *LookupTableGV =
        new GlobalVariable(M, TableType, true, GlobalValue::InternalLinkage,
                           TableInit, "__obf_char_table");

    /*
    int val = 0, bits = -8;
    */
    Value *Val = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "val");
    Builder.CreateStore(Builder.getInt32(0), Val);

    Value *Bits = Builder.CreateAlloca(Builder.getInt32Ty(), nullptr, "bits");
    Builder.CreateStore(Builder.getInt32(-8), Bits);

    /*
    size_t out_pos = 0;
    */

    Value *OutPos =
        Builder.CreateAlloca(Builder.getInt64Ty(), nullptr, "out_pos");
    Builder.CreateStore(Builder.getInt64(0), OutPos);

    /*
    for (size_t i = 0; i < length;
    in this loop header:
    for (size_t i = 0; i < length; i++) {
    */

    BasicBlock *LoopHeader = BasicBlock::Create(Ctx, "loop_header", F);
    Builder.CreateBr(LoopHeader);
    Builder.SetInsertPoint(LoopHeader);

    PHINode *IndexPhi = Builder.CreatePHI(Builder.getInt64Ty(), 2, "phi_idx");
    IndexPhi->addIncoming(Builder.getInt64(0), Entry);

    BasicBlock *LoopBody = BasicBlock::Create(Ctx, "loop_body", F);
    BasicBlock *LoopExit = BasicBlock::Create(Ctx, "loop_exit", F);

    Value *Cond = Builder.CreateICmpULT(IndexPhi, Len, "cond");
    Builder.CreateCondBr(Cond, LoopBody, LoopExit);

    /*
    unsigned char c = input[i];
    */

    Builder.SetInsertPoint(LoopBody);

    Value *InputGEP = Builder.CreateInBoundsGEP(Builder.getInt8Ty(), EncodedPtr,
                                                IndexPhi, "input_gep");
    Value *Char = Builder.CreateLoad(Builder.getInt8Ty(), InputGEP, "char");

    /*
    T[c]
    in this statement:
    val = (val << 6) + T[c];
    */

    Value *Zero = Builder.getInt32(0);

    Value *TableGEP = Builder.CreateInBoundsGEP(TableType, LookupTableGV,
                                                {Zero, Char}, "table_gep");
    Value *TC = Builder.CreateLoad(Builder.getInt32Ty(), TableGEP, "tc");

    /*
    val = (val << 6) +
    in this statement:
    val = (val << 6) + T[c];
    */

    Value *ValLoaded =
        Builder.CreateLoad(Builder.getInt32Ty(), Val, "val_loaded");
    Value *ValShifted =
        Builder.CreateShl(ValLoaded, Builder.getInt32(6), "val_shifted");
    Value *ValNew = Builder.CreateAdd(ValShifted, TC, "val_new");
    Builder.CreateStore(ValNew, Val);

    /*
    bits += 6;
    */

    Value *BitsLoaded =
        Builder.CreateLoad(Builder.getInt32Ty(), Bits, "bits_loaded");
    Value *BitsNew =
        Builder.CreateAdd(BitsLoaded, Builder.getInt32(6), "bits_new");
    Builder.CreateStore(BitsNew, Bits);

    /*
    if (bits >= 0) {
    */
    Value *BitsCheck =
        Builder.CreateICmpSGE(BitsNew, Builder.getInt32(0), "bits_check");
    BasicBlock *StoreByteBB = BasicBlock::Create(Ctx, "store_byte", F);
    BasicBlock *LoopIncBB = BasicBlock::Create(Ctx, "loop_inc", F);
    Builder.CreateCondBr(BitsCheck, StoreByteBB, LoopIncBB);

    Builder.SetInsertPoint(StoreByteBB);

    Value *OutPosLoaded =
        Builder.CreateLoad(Builder.getInt64Ty(), OutPos, "out_pos_loaded");

    /*
    (char)((val >> bits) & 0xFF)
    in this statament:
    input[out_pos++] = (char)((val >> bits) & 0xFF);
    */
    Value *Shifted = Builder.CreateLShr(ValNew, BitsNew, "shifted");
    Value *Masked =
        Builder.CreateAnd(Shifted, Builder.getInt32(0xFF), "masked");
    Value *ByteValue = Builder.CreateTrunc(Masked, Builder.getInt8Ty(), "byte");

    /*
    input[out_pos] =
    in this statament:
    input[out_pos++] = (char)((val >> bits) & 0xFF);
    */
    Value *OutputGEP = Builder.CreateInBoundsGEP(
        Builder.getInt8Ty(), EncodedPtr, OutPosLoaded, "output_gep");
    Builder.CreateStore(ByteValue, OutputGEP);

    /*
    out_pos++
    in this statament:
    input[out_pos++] = (char)((val >> bits) & 0xFF);
    */
    Value *OutPosInc =
        Builder.CreateAdd(OutPosLoaded, Builder.getInt64(1), "out_pos_inc");
    Builder.CreateStore(OutPosInc, OutPos);

    /*
    bits -= 8;
    */
    Value *BitsDecremented =
        Builder.CreateSub(BitsNew, Builder.getInt32(8), "bits_dec");
    Builder.CreateStore(BitsDecremented, Bits);

    Builder.CreateBr(LoopIncBB);
    Builder.SetInsertPoint(LoopIncBB);

    /*
    i++
    in this loop header:
    for (size_t i = 0; i < length; i++) {
    */
    Value *NextIndex =
        Builder.CreateAdd(IndexPhi, Builder.getInt64(1), "next_idx");
    IndexPhi->addIncoming(NextIndex, LoopIncBB);

    /*
    continue
    */
    Builder.CreateBr(LoopHeader);

    Builder.SetInsertPoint(LoopExit);
    Builder.CreateRetVoid();

    return F;
  }
};
} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "string-base64-encode") {
          MPM.addPass(StringBase64EncodePass());
          return true;
        }
        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "StringBase64EncodePass",
          LLVM_VERSION_STRING, registerPass};
}