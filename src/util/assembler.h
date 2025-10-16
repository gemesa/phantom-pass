#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <string>

class Assembler {
private:
  llvm::Triple TheTriple;
  std::unique_ptr<llvm::LLVMContext> Ctx;

public:
  explicit Assembler(const llvm::Triple &TT);
  llvm::SmallVector<uint8_t, 32> assemble(const std::string &AsmCode,
                                          size_t NumInstructions);
};

#endif // ASSEMBLER_H