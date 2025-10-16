#include "assembler.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;

Assembler::Assembler(const Triple &TT)
    : TheTriple(TT), Ctx(std::make_unique<LLVMContext>()) {
  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();
}

SmallVector<uint8_t, 32> Assembler::assemble(const std::string &AsmCode,
                                             size_t NumInstructions) {
  auto M = std::make_unique<Module>("__asm_module", *Ctx);

  Function *F =
      Function::Create(FunctionType::get(Type::getVoidTy(*Ctx), {}, false),
                       Function::ExternalLinkage, "__asm_func", M.get());

  BasicBlock *BB = BasicBlock::Create(*Ctx, "entry", F);
  IRBuilder<> IRB(BB);

  auto *FType = FunctionType::get(IRB.getVoidTy(), false);
  InlineAsm *RawAsm = InlineAsm::get(FType, AsmCode, "",
                                     true,  // hasSideEffects
                                     true); // isStackAligned
  IRB.CreateCall(FType, RawAsm);
  IRB.CreateRetVoid();

  ExitOnError ExitOnErr;
  orc::LLJITBuilder Builder;
  orc::JITTargetMachineBuilder JTMB(TheTriple);
  JTMB.setRelocationModel(Reloc::Model::PIC_);
  JTMB.setCodeModel(CodeModel::Large);
  JTMB.setCodeGenOptLevel(CodeGenOptLevel::None);

  Builder.setJITTargetMachineBuilder(std::move(JTMB));

  auto JIT = ExitOnErr(Builder.create());
  M->setDataLayout(JIT->getDataLayout());

  auto &IRC = JIT->getIRCompileLayer();
  orc::IRCompileLayer::IRCompiler &Compiler = IRC.getCompiler();

  auto Res = Compiler(*M);
  std::unique_ptr<MemoryBuffer> ObjBuffer = ExitOnErr(std::move(Res));

  size_t FunSize = NumInstructions * 4; // AArch64: 4 bytes per instruction

  ExitOnErr(JIT->addObjectFile(std::move(ObjBuffer)));
  auto Addr = ExitOnErr(JIT->lookup("__asm_func"));

  auto *Ptr = reinterpret_cast<const uint8_t *>(Addr.getValue());
  return SmallVector<uint8_t, 32>(Ptr, Ptr + FunSize);
}