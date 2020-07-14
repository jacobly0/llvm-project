//===-- Z80ELFStreamer.cpp - Z80 ELF Streamer -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Z80ELFStreamer.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/FormattedStream.h"

namespace llvm {

Z80ELFStreamer::Z80ELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI)
    : Z80TargetStreamer(S) {
  MCAssembler &MCA = getStreamer().getAssembler();
  unsigned EFlags = MCA.getELFHeaderEFlags();

  EFlags |= ELF::EF_Z80_MACH_Z80;
  MCA.setELFHeaderEFlags(EFlags);
}

} // end namespace llvm
