//===-- Z80ELFObjectWriter.cpp - Z80 ELF Writer ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80FixupKinds.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"

#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

#include <cstdint>
#include <memory>

namespace llvm {

/// Writes Z80 machine code into an ELF32 object file.
class Z80ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  Z80ELFObjectWriter(uint8_t OSABI);

  virtual ~Z80ELFObjectWriter() {}

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsPCRel) const override;
};

Z80ELFObjectWriter::Z80ELFObjectWriter(uint8_t OSABI)
    : MCELFObjectTargetWriter(false, OSABI, ELF::EM_Z80, true) {}

unsigned Z80ELFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                          const MCFixup &Fixup,
                                          bool IsPCRel) const {
  const MCSymbolRefExpr *SymRef = dyn_cast<MCSymbolRefExpr>(Fixup.getValue());

  switch (static_cast<unsigned>(Fixup.getKind())) {
  case FK_Data_1:
  case Z80::fixup_8:
    return ELF::R_Z80_8;
  case Z80::fixup_8_dis:
    return ELF::R_Z80_8_DIS;
  case Z80::fixup_8_pcrel:
    assert(IsPCRel);
    return ELF::R_Z80_8_PCREL;
  case FK_Data_2:
  case Z80::fixup_16:
    return ELF::R_Z80_16;
  case Z80::fixup_24:
    return ELF::R_Z80_24;
  case FK_Data_4:
  case Z80::fixup_32:
    return ELF::R_Z80_32;
  case Z80::fixup_byte0:
    return ELF::R_Z80_BYTE0;
  case Z80::fixup_byte1:
    return ELF::R_Z80_BYTE1;
  case Z80::fixup_byte2:
    return ELF::R_Z80_BYTE2;
  case Z80::fixup_byte3:
    return ELF::R_Z80_BYTE3;
  case Z80::fixup_word0:
    return ELF::R_Z80_WORD0;
  case Z80::fixup_word1:
    return ELF::R_Z80_WORD1;
  case Z80::fixup_16_be:
    return ELF::R_Z80_16_BE;
  default:
    llvm_unreachable(Twine("Invalid fixup kind! ",
                           SymRef ? SymRef->getSymbol().getName().str()
                                  : "(not even a symref!)")
                         .str()
                         .c_str());
  }
}

std::unique_ptr<MCObjectTargetWriter> createZ80ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<Z80ELFObjectWriter>(OSABI);
}

} // end of namespace llvm
