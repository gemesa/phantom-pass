#ifndef DISASSEMBLER_H
#define DISASSEMBLER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/TargetParser/Triple.h"
#include <memory>
#include <string>

namespace llvm {
class Target;
}

class Disassembler {
private:
  const llvm::Target *TheTarget;
  std::unique_ptr<llvm::MCRegisterInfo> MRI;
  std::unique_ptr<llvm::MCAsmInfo> MAI;
  std::unique_ptr<llvm::MCSubtargetInfo> STI;
  std::unique_ptr<llvm::MCInstrInfo> MII;
  std::unique_ptr<llvm::MCContext> Ctx;
  std::unique_ptr<llvm::MCDisassembler> DisAsm;
  std::unique_ptr<llvm::MCInstPrinter> IP;

public:
  explicit Disassembler(const llvm::Triple &TT);
  bool isValid() const;
  std::string disassemble(llvm::ArrayRef<uint8_t> Bytes);
};

#endif // DISASSEMBLER_H