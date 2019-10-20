//===-- Z80TargetFrameLowering.h - Define frame lowering for Z80 -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class implements z80-specific bits of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80FRAMELOWERING_H
#define LLVM_LIB_TARGET_Z80_Z80FRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"

namespace llvm {
class Z80Subtarget;
class Z80InstrInfo;
class Z80RegisterInfo;

class Z80FrameLowering : public TargetFrameLowering {
  const Z80Subtarget &STI;
  const Z80InstrInfo &TII;
  const Z80RegisterInfo *TRI;

  bool Is24Bit;
  unsigned SlotSize;

public:
  explicit Z80FrameLowering(const Z80Subtarget &STI);

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  bool assignCalleeSavedSpillSlots(
      MachineFunction &MF, const TargetRegisterInfo *TRI,
      std::vector<CalleeSavedInfo> &CSI) const override;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const std::vector<CalleeSavedInfo> &CSI,
                                 const TargetRegisterInfo *TRI) const override;
  bool restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   std::vector<CalleeSavedInfo> &CSI,
                                   const TargetRegisterInfo *TRI) const override;

  void processFunctionBeforeFrameFinalized(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;

  MachineBasicBlock::iterator eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const override;

  bool hasFP(const MachineFunction &MF) const override;

  enum StackAdjustmentMethod {
    SAM_None,
    SAM_Tiny,
    SAM_All,
    SAM_Small,
    SAM_Medium,
    SAM_Large
  };
  StackAdjustmentMethod getOptimalStackAdjustmentMethod(
      MachineFunction &MF, int Offset, int FPOffset = -1,
      bool ScratchIsIndex = false, bool UnknownOffset = false) const;

private:
  void BuildStackAdjustment(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, DebugLoc DL,
                            Register ScratchReg, int Offset, int FPOffset = -1,
                            bool UnknownOffset = false) const;

  void shadowCalleeSavedRegisters(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, DebugLoc DL,
      MachineInstr::MIFlag Flag, const std::vector<CalleeSavedInfo> &CSI) const;
};
} // End llvm namespace

#endif
