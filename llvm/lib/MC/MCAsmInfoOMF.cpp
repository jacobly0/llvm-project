//===- MCAsmInfoOMF.cpp - OMF asm properties ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines target asm properties related what form asm statements
// should take in general on OMF-based targets
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfoOMF.h"

using namespace llvm;

void MCAsmInfoOMF::anchor() {}

MCAsmInfoOMF::MCAsmInfoOMF() {
  PrivateLabelPrefix = "L";
}
