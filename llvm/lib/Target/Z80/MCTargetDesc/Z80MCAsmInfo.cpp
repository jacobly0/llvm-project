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
  Code16Directive = "assume\tadl = 0";
  Code24Directive = "assume\tadl = 1";
  Code32Directive = Code64Directive = nullptr;
  AssemblerDialect = !Is16Bit;
  SupportsQuotedNames = false;
  ZeroDirective = AsciiDirective = AscizDirective = nullptr;
  BlockSeparator = " dup ";
  Data8bitsDirective = "\tdb\t";
  Data16bitsDirective = "\tdw\t";
  Data24bitsDirective = "\tdl\t";
  Data32bitsDirective = "\tdd\t";
  Data64bitsDirective = nullptr;
  AlwaysChangeSection = true;
  GlobalDirective = "\tpublic\t";
  LGloblDirective = "\tprivate\t";
  SetDirective = "\tlabel\t";
  SetSeparator = " at ";
  HasFunctionAlignment = false;
  HasDotTypeDotSizeDirective = false;
  WeakDirective = "\tweak\t";
  UseIntegratedAssembler = false;
  UseLogicalShr = false;
  HasSingleParameterDotFile = false;
}

const char *Z80MCAsmInfo::getBlockDirective(int64_t Size) const {
  switch (Size) {
  default: return nullptr;
  case 1: return "\tdb\t";
  case 2: return "\tdw\t";
  case 3: return "\tdl\t";
  case 4: return "\tdd\t";
  }
}
