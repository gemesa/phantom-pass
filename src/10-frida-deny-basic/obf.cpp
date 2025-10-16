/*
The documentation is available here:
https://shadowshell.io/phantom-pass/10-frida-deny-basic.html
*/
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
  std::set<std::string> FunctionNames;

public:
  FridaDenyPass() = default;
  FridaDenyPass(std::set<std::string> Names)
      : FunctionNames(std::move(Names)) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = false;

    const auto &TT = Triple(M.getTargetTriple());
    if (!TT.isAArch64()) {
      errs() << "FridaDenyPass: Only AArch64 is supported\n";
      return PreservedAnalyses::all();
    }

    for (Function &F : M) {
      if (!FunctionNames.empty() &&
          FunctionNames.count(F.getName().str()) == 0) {
        continue;
      }
      if (F.isDeclaration() || F.getInstructionCount() == 0) {
        continue;
      }
      if (F.hasPrologueData()) {
        errs() << "FridaDenyPass: Function " << F.getName()
               << " already has some prologue data\n";
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
  bool injectFridaPrologue(Function &F) {

    uint8_t PrologueBytes[] = {
        // $ echo "mov x16, x16" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x16, x16 // encoding: [0xf0,0x03,0x10,0xaa]
        0xF0, 0x03, 0x10, 0xAA,
        // $ echo "mov x17, x17" | llvm-mc -triple=aarch64 -show-encoding
        // mov	x17, x17 // encoding: [0xf1,0x03,0x11,0xaa]
        0xF1, 0x03, 0x11, 0xAA};

    LLVMContext &Ctx = F.getContext();
    auto *Prologue = ConstantDataVector::getRaw(
        StringRef(reinterpret_cast<const char *>(PrologueBytes),
                  sizeof(PrologueBytes)),
        sizeof(PrologueBytes), Type::getInt8Ty(Ctx));

    F.setPrologueData(Prologue);
    return true;
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
            std::set<std::string> Functions(Parts.begin(), Parts.end());
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