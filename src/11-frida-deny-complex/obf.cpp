/*
The documentation is available here:
https://shadowshell.io/phantom-pass/11-frida-deny-complex.html
*/
#include "assembler.h"
#include "disassembler.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <set>

using namespace llvm;

namespace {

class FridaDenyPass : public PassInfoMixin<FridaDenyPass> {
private:
  SmallSet<StringRef, 8> FunctionNames;
  std::unique_ptr<Disassembler> Disasm;
  std::unique_ptr<Assembler> Asm;

public:
  FridaDenyPass() = default;
  FridaDenyPass(SmallSet<StringRef, 8> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;
    const auto &TT = Triple(M.getTargetTriple());
    if (!TT.isAArch64()) {
      errs() << "FridaDenyPass: Only AArch64 is supported\n";
      return PreservedAnalyses::all();
    }

    Disasm = std::make_unique<Disassembler>(TT);
    Asm = std::make_unique<Assembler>(TT);

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }
      if (F.isDeclaration()) {
        continue;
      }

      if (injectFridaPrologue(F)) {
        Changed = true;
        outs() << "FridaDenyPass: Injected frida deny prologue into function '"
               << F.getName() << "'\n";
      }
    }
    return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
  }

private:
  void printPrologueData(Function &F) {
    if (!F.hasPrologueData()) {
      return;
    }

    Constant *Existing = F.getPrologueData();
    if (auto *DataVec = dyn_cast<ConstantDataVector>(Existing)) {
      SmallVector<uint8_t, 32> Bytes;
      for (unsigned i = 0; i < DataVec->getNumElements(); ++i) {
        Bytes.push_back(DataVec->getElementAsInteger(i));
      }

      outs() << "Function '" << F.getName() << "' already has prologue data!\n";

      if (Disasm && Disasm->isValid()) {
        outs() << "Disassembly:\n";
        outs() << Disasm->disassemble(Bytes);
      }
    }
  }

  bool appendPrologueData(Function &F, ArrayRef<uint8_t> NewBytes) {
    SmallVector<uint8_t, 32> CombinedBytes;

    printPrologueData(F);

    if (F.hasPrologueData()) {
      Constant *Existing = F.getPrologueData();
      if (auto *DataVec = dyn_cast<ConstantDataVector>(Existing)) {
        for (unsigned i = 0; i < DataVec->getNumElements(); ++i) {
          CombinedBytes.push_back(DataVec->getElementAsInteger(i));
        }
      }
    }

    CombinedBytes.append(NewBytes.begin(), NewBytes.end());

    auto *Int8Ty = Type::getInt8Ty(F.getContext());
    auto *Prologue = ConstantDataVector::getRaw(
        StringRef(reinterpret_cast<const char *>(CombinedBytes.data()),
                  CombinedBytes.size()),
        CombinedBytes.size(), Int8Ty);

    F.setPrologueData(Prologue);
    return true;
  }

  bool injectFridaPrologue(Function &F) {
    std::string AsmCode = "mov x16, x16\n"
                          "mov x17, x17";
    SmallVector<uint8_t, 32> MachineCode = Asm->assemble(AsmCode, 2);
    return appendPrologueData(F, MachineCode);
  }
};

} // anonymous namespace

static void registerPass(PassBuilder &PB) {
  PB.registerPipelineParsingCallback(
      [](StringRef Name, ModulePassManager &MPM,
         ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "frida-deny") {
          MPM.addPass(FridaDenyPass());
          return true;
        }
        if (Name.consume_front("frida-deny<")) {
          if (Name.consume_back(">")) {
            SmallVector<StringRef, 4> Parts;
            Name.split(Parts, ';', -1, false);
            SmallSet<StringRef, 8> Functions(Parts.begin(), Parts.end());
            MPM.addPass(FridaDenyPass(std::move(Functions)));
            return true;
          }
        }
        return false;
      });
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "FridaDenyPass", LLVM_VERSION_STRING,
          registerPass};
}