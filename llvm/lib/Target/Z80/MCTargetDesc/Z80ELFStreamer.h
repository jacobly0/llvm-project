//===----- Z80ELFStreamer.h - Z80 ELF Streamer -----------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_Z80_ELF_STREAMER_H
#define LLVM_Z80_ELF_STREAMER_H

#include "Z80TargetStreamer.h"

#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"

#include <cstdint>

namespace llvm {

/// A target streamer for an Z80 ELF object file.
class Z80ELFStreamer : public Z80TargetStreamer {
public:
  Z80ELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }

  void emitAlign(unsigned u) override {}
  void emitBlock(uint64_t u) override {}
  void emitLocal(MCSymbol *S) override {}
  void emitGlobal(MCSymbol *S) override {}
  void emitExtern(MCSymbol *S) override {}
};

} // end namespace llvm

#endif
