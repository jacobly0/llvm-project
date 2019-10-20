//===-- Z80MCAsmInfo.cpp - Z80 asm properties -----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the Z80MCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "Z80MCAsmInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
using namespace llvm;

void Z80MCAsmInfo::anchor() { }

Z80MCAsmInfo::Z80MCAsmInfo(const Triple &T) {
  bool Is16Bit = T.isArch16Bit() || T.getEnvironment() == Triple::CODE16;
  CodePointerSize = CalleeSaveStackSlotSize = Is16Bit ? 2 : 3;
  MaxInstLength = 6;
  DollarIsPC = true;
  SeparatorString = nullptr;
  CommentString = ";";
  PrivateGlobalPrefix = PrivateLabelPrefix = "";
  Code16Directive = ".assume\tadl = 0";
  Code24Directive = ".assume\tadl = 1";
  Code32Directive = Code64Directive = nullptr;
  AssemblerDialect = !Is16Bit;
  SupportsQuotedNames = false;
  ZeroDirective = AsciiDirective = AscizDirective = nullptr;
  Data8bitsDirective = "\tDB\t";
  Data16bitsDirective = "\tDW\t";
  Data24bitsDirective = "\tDW24\t";
  Data32bitsDirective = "\tDL\t";
  Data64bitsDirective = nullptr;
  GlobalDirective = "\tXDEF\t";
  HasFunctionAlignment = false;
  HasDotTypeDotSizeDirective = false;
  WeakDirective = nullptr;
  UseIntegratedAssembler = false;
  WeakDirective = nullptr;
  UseLogicalShr = false;
}

const char *Z80MCAsmInfo::getBlockDirective(int64_t Size) const {
  switch (Size) {
  default: return nullptr;
  case 1: return "\tBLKB\t";
  case 2: return "\tBLKW\t";
  case 3: return "\tBLKP\t";
  case 4: return "\tBLKL\t";
  }
}
