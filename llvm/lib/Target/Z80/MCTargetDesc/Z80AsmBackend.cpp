//===-- Z80AsmBackend.cpp - Z80 Asm Backend  ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Z80AsmBackend class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80AsmBackend.h"
#include "MCTargetDesc/Z80FixupKinds.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"

#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <memory>

namespace llvm {

std::unique_ptr<MCObjectTargetWriter>
Z80AsmBackend::createObjectTargetWriter() const {
  return createZ80ELFObjectWriter(ELF::ELFOSABI_STANDALONE);
}

MCFixupKindInfo const &Z80AsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[Z80::NumTargetFixupKinds] = {
      // This table *must* be in same the order of fixup_* kinds in
      // Z80FixupKinds.h.
      //
      // name           offset  bits  flags
      {"fixup_8", 0U, 8U, 0U},
      {"fixup_8_dis", 0U, 8U, 0U},
      {"fixup_8_pcrel", 0U, 8U, MCFixupKindInfo::FKF_IsPCRel},
      {"fixup_16", 0U, 16U, 0U},
      {"fixup_24", 0U, 24U, 0U},
      {"fixup_32", 0U, 32U, 0U},
      {"fixup_byte0", 0U, 32U, 0U},
      {"fixup_byte1", 0U, 32U, 0U},
      {"fixup_byte2", 0U, 32U, 0U},
      {"fixup_byte3", 0U, 32U, 0U},
      {"fixup_word0", 0U, 32U, 0U},
      {"fixup_word1", 0U, 32U, 0U},
      {"fixup_16_be", 0U, 16U, 0U}};

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);
  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  return Infos[Kind - FirstTargetFixupKind];
}

bool Z80AsmBackend::shouldForceRelocation(const MCAssembler &Asm,
                                          const MCFixup &Fixup,
                                          const MCValue &Target) {
  switch ((unsigned)Fixup.getKind()) {
  default:
    return false;
  // Fixups which should always be recorded as relocations.
  case Z80::fixup_8_dis:
  case Z80::fixup_8_pcrel:
  case Z80::fixup_16:
    return true;
  }
}

MCAsmBackend *createZ80AsmBackend(const Target &T, const MCSubtargetInfo &STI,
                                  const MCRegisterInfo &MRI,
                                  const llvm::MCTargetOptions &TO) {
  return new Z80AsmBackend();
}

} // end of namespace llvm
