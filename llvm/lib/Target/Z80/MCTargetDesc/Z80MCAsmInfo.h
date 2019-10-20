//===-- Z80MCAsmInfo.h - Z80 asm properties --------------------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the Z80MCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80MCASMINFO_H
#define LLVM_LIB_TARGET_Z80_MCTARGETDESC_Z80MCASMINFO_H

#include "llvm/MC/MCAsmInfoOMF.h"

namespace llvm {
class Triple;

class Z80MCAsmInfo : public MCAsmInfoOMF {
  void anchor() override;

public:
  explicit Z80MCAsmInfo(const Triple &Triple);

  const char *getBlockDirective(int64_t Size) const override;
};
} // End llvm namespace

#endif
