#include "disassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Disassembler::Disassembler(const Triple &TT) {
  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  std::string Error;
  TheTarget = TargetRegistry::lookupTarget(TT.str(), Error);
  if (!TheTarget) {
    errs() << "Error: " << Error << "\n";
    return;
  }

  MRI.reset(TheTarget->createMCRegInfo(TT.str()));
  MAI.reset(TheTarget->createMCAsmInfo(*MRI, TT.str(), MCTargetOptions()));
  STI.reset(TheTarget->createMCSubtargetInfo(TT.str(), "", ""));
  MII.reset(TheTarget->createMCInstrInfo());

  Ctx = std::make_unique<MCContext>(TT, MAI.get(), MRI.get(), STI.get());
  DisAsm.reset(TheTarget->createMCDisassembler(*STI, *Ctx));

  IP.reset(TheTarget->createMCInstPrinter(TT, MAI->getAssemblerDialect(), *MAI,
                                          *MII, *MRI));
  if (IP)
    IP->setPrintImmHex(true);
}

bool Disassembler::isValid() const { return TheTarget && DisAsm && IP; }

std::string Disassembler::disassemble(ArrayRef<uint8_t> Bytes) {
  if (!isValid())
    return "<disassembler not available>";

  std::string Result;
  raw_string_ostream OS(Result);

  ArrayRef<uint8_t> Data(Bytes);
  uint64_t Addr = 0;

  while (!Data.empty()) {
    MCInst Inst;
    uint64_t Size;
    MCDisassembler::DecodeStatus S =
        DisAsm->getInstruction(Inst, Size, Data, Addr, nulls());

    if (S == MCDisassembler::Success) {
      IP->printInst(&Inst, Addr, "", *STI, OS);
      OS << "\n";
      Data = Data.slice(Size);
      Addr += Size;
    } else {
      OS << format("<invalid: 0x%02X>\n", Data[0]);
      Data = Data.slice(1);
      Addr += 1;
    }
  }

  return OS.str();
}