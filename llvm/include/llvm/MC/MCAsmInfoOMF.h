//===-- llvm/MC/MCAsmInfoOMF.h - OMF Asm info -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCASMINFOWASM_H
#define LLVM_MC_MCASMINFOWASM_H

#include "llvm/MC/MCAsmInfo.h"

namespace llvm {
class MCAsmInfoOMF : public MCAsmInfo {
  virtual void anchor();

protected:
  MCAsmInfoOMF();
};
}

#endif // LLVM_MC_MCASMINFOWASM_H
