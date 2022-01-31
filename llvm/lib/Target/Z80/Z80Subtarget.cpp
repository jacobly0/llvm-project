//===-- Z80Subtarget.cpp - Z80 Subtarget Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Z80 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "Z80Subtarget.h"
#include "GISel/Z80CallLowering.h"
#include "GISel/Z80InlineAsmLowering.h"
#include "GISel/Z80LegalizerInfo.h"
#include "GISel/Z80RegisterBankInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80TargetMachine.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
using namespace llvm;

#define DEBUG_TYPE "z80-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "Z80GenSubtargetInfo.inc"

Z80Subtarget &Z80Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef TuneCPU,
                                                            StringRef FS) {
  if (CPU.empty())
    CPU = TargetTriple.getArchName();
  ParseSubtargetFeatures(CPU, TuneCPU, FS);
  HasIdxHalfRegs = HasUndocOps || HasEZ80Ops;
  HasSliOp = HasUndocOps;
  return *this;
}

Z80Subtarget::Z80Subtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU,
                           StringRef FS, const Z80TargetMachine &TM)
    : Z80GenSubtargetInfo(TT, CPU, TuneCPU, FS), TargetTriple(TT),
      In16BitMode(TT.isArch16Bit() || TT.getEnvironment() == Triple::CODE16),
      In24BitMode(!In16BitMode),
      InstrInfo(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      TLInfo(TM, *this), FrameLowering(*this) {
  CallLoweringInfo.reset(new Z80CallLowering(*getTargetLowering()));
  InlineAsmLoweringInfo.reset(new Z80InlineAsmLowering(*getTargetLowering()));
  Legalizer.reset(new Z80LegalizerInfo(*this, TM));

  auto *RBI = new Z80RegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);
  InstSelector.reset(createZ80InstructionSelector(TM, *this, *RBI));
}
