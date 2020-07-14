//===-- Z80AsmBackend.h - Z80 Asm Backend  --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file The Z80 assembly backend implementation.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_Z80_ASM_BACKEND_H
#define LLVM_Z80_ASM_BACKEND_H

#include "MCTargetDesc/Z80FixupKinds.h"

#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <memory>

namespace llvm {

class MCAsmLayout;
class MCAssembler;
class MCInst;
class MCObjectWriter;
class MCRelaxableFragment;
class MCSubtargetInfo;
class MCValue;
class Target;

struct MCFixupKindInfo;

/// Utilities for manipulating generated Z80 machine code.
class Z80AsmBackend : public MCAsmBackend {
public:
  Z80AsmBackend() : MCAsmBackend(support::little) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override {}

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  unsigned getNumFixupKinds() const override {
    return Z80::NumTargetFixupKinds;
  }

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override {
    return false;
  }

  bool fixupNeedsRelaxation(const MCFixup &Fixup, uint64_t Value,
                            const MCRelaxableFragment *DF,
                            const MCAsmLayout &Layout) const override {
    llvm_unreachable("RelaxInstruction() unimplemented");
    return false;
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count) const override {
    OS.write_zeros(Count);
    return true;
  }

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target) override;
};

} // end namespace llvm

#endif // LLVM_Z80_ASM_BACKEND_H
