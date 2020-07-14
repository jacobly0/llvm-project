//===-- Z80MCCodeEmitter.h - Convert Z80 Code to Machine Code -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Z80MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_Z80_CODE_EMITTER_H
#define LLVM_Z80_CODE_EMITTER_H

#include "Z80FixupKinds.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/Support/DataTypes.h"

#define GET_INSTRINFO_OPERAND_TYPES_ENUM
#include "Z80GenInstrInfo.inc"

#include <cstdint>

namespace llvm {

class MCContext;
class MCFixup;
class MCInst;
class MCInstrInfo;
class MCSubtargetInfo;
class raw_ostream;

/// Writes Z80 machine code to a stream.
class Z80MCCodeEmitter : public MCCodeEmitter {
public:
  Z80MCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx) : MCII(MCII) {}

private:
  void emitByte(uint8_t C, unsigned &CurByte, raw_ostream &OS) const;

  void encodeInstruction(const MCInst &MI, raw_ostream &OS,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

  Z80MCCodeEmitter(const Z80MCCodeEmitter &) = delete;
  void operator=(const Z80MCCodeEmitter &) = delete;

  const MCInstrInfo &MCII;
};

} // namespace llvm

#endif // LLVM_Z80_CODE_EMITTER_H
