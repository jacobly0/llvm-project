//===-- Z80Subtarget.h - Define Subtarget for the Z80 ----------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the Z80 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80SUBTARGET_H
#define LLVM_LIB_TARGET_Z80_Z80SUBTARGET_H

#include "GISel/Z80CallLowering.h"
#include "GISel/Z80InlineAsmLowering.h"
#include "GISel/Z80LegalizerInfo.h"
#include "GISel/Z80RegisterBankInfo.h"
#include "Z80FrameLowering.h"
#include "Z80ISelLowering.h"
#include "Z80InstrInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "Z80GenSubtargetInfo.inc"

namespace llvm {
class Z80TargetMachine;

class Z80Subtarget final : public Z80GenSubtargetInfo {
  /// What processor and OS we're targeting.
  Triple TargetTriple;

  /// True if compiling for 16-bit, false for 24-bit.
  bool In16BitMode;

  /// True if compiling for 24-bit, false for 16-bit.
  bool In24BitMode;

  /// True if target has undocumented z80 instructions.
  bool HasUndocOps = false;

  /// True if target has z180 instructions.
  bool HasZ180Ops = false;

  /// True if target has ez80 instructions.
  bool HasEZ80Ops = false;

  /// True if target has index half registers (HasUndocOps || HasEZ80Ops).
  bool HasIdxHalfRegs = false;

  /// True if target has SLI (also known SLL and SL1) instruction (HasUndocOps)
  bool HasSliOp = false;

  // Ordering here is important. Z80InstrInfo initializes Z80RegisterInfo which
  // Z80TargetLowering needs.
  Z80InstrInfo InstrInfo;
  Z80TargetLowering TLInfo;
  Z80FrameLowering FrameLowering;

  /// GlobalISel related APIs.
  std::unique_ptr<Z80CallLowering> CallLoweringInfo;
  std::unique_ptr<Z80InlineAsmLowering> InlineAsmLoweringInfo;
  std::unique_ptr<InstructionSelector> InstSelector;
  std::unique_ptr<Z80LegalizerInfo> Legalizer;
  std::unique_ptr<Z80RegisterBankInfo> RegBankInfo;

public:
  /// This constructor initializes the data members to match that
  /// of the specified triple.
  Z80Subtarget(const Triple &TT, StringRef CPU, StringRef TuneCPU, StringRef FS,
               const Z80TargetMachine &TM);

  const Z80TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }
  const Z80InstrInfo *getInstrInfo() const override { return &InstrInfo; }
  const Z80FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }
  const Z80RegisterInfo *getRegisterInfo() const override {
    return &getInstrInfo()->getRegisterInfo();
  }

  /// ParseSubtargetFeatures - Parses features string setting specified
  /// subtarget options.  Definition of function is auto generated by tblgen.
  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  /// Methods used by Global ISel
  const Z80CallLowering *getCallLowering() const override {
    return CallLoweringInfo.get();
  }
  const Z80InlineAsmLowering *getInlineAsmLowering() const override {
    return InlineAsmLoweringInfo.get();
  }
  InstructionSelector *getInstructionSelector() const override {
    return InstSelector.get();
  }
  const LegalizerInfo *getLegalizerInfo() const override {
    return Legalizer.get();
  }
  const RegisterBankInfo *getRegBankInfo() const override {
    return RegBankInfo.get();
  }

private:
  Z80Subtarget &initializeSubtargetDependencies(StringRef CPU,
                                                StringRef TuneCPU,
                                                StringRef FS);
  void initializeEnvironment();

public:
  const Triple &getTargetTriple() const { return TargetTriple; }
  /// Is this ez80 (disregarding specific ABI / programming model)
  bool is24Bit() const { return In24BitMode; }
  bool is16Bit() const { return In16BitMode; }
  bool hasUndocOps() const { return HasUndocOps; }
  bool hasZ180Ops() const { return HasZ180Ops; }
  bool hasEZ80Ops() const { return HasEZ80Ops; }
  bool hasIndexHalfRegs() const { return HasIdxHalfRegs; }
  bool hasSliOp() const { return HasSliOp; }
  bool has24BitEZ80Ops() const { return is24Bit() && hasEZ80Ops(); }
  bool has16BitEZ80Ops() const { return is16Bit() && hasEZ80Ops(); }
};
} // namespace llvm

#endif
