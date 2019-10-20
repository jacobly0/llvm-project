//===-- Z80RegisterInfo.h - Z80 Register Information Impl -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Z80 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80REGISTERINFO_H
#define LLVM_LIB_TARGET_Z80_Z80REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "Z80GenRegisterInfo.inc"

namespace llvm {
class Triple;

class Z80RegisterInfo final : public Z80GenRegisterInfo {
  /// Is24bit - Is the target 24-bits.
  ///
  bool Is24Bit;

  /// SlotSize - Stack slot size in bytes.
  ///
  unsigned SlotSize;

  /// StackPtr - Z80 physical register used as stack ptr.
  ///
  unsigned StackPtr;

public:
  Z80RegisterInfo(const Triple &TT);

  /// Code Generation virtual methods...
  ///
  bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const override {
    // Z80MachineLateOptimization requires liveness.
    return true;
  }

  /// getPointerRegClass - Returns a TargetRegisterClass used for pointer
  /// values.
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;
  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  /// getCalleeSavedRegs - Return a null-terminated list of all of the
  /// callee-save registers on this target.
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  const uint32_t *getNoPreservedMask() const override;

  /// getReservedRegs - Returns a bitset indexed by physical register number
  /// indicating if a register is a special register that has particular uses
  /// and should be considered unavailable at all times, e.g. SP, RA.  This is
  /// used by register scaverger to determine what registers are free.
  BitVector getReservedRegs(const MachineFunction &MF) const override;

  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }
  bool requiresFrameIndexScavenging(
      const MachineFunction &MF) const override {
    return true;
  }
  bool requiresFrameIndexReplacementScavenging(
      const MachineFunction &MF) const override {
    return true;
  }

  bool saveScavengerRegister(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MI,
                             MachineBasicBlock::iterator &UseMI,
                             const TargetRegisterClass *RC,
                             unsigned Reg) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator MI,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Support for virtual base registers.
  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override;
  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  void materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                    unsigned BaseReg, int FrameIdx,
                                    int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, unsigned BaseReg,
                         int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI, unsigned BaseReg,
                          int64_t Offset) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;
  Register getStackRegister() const { return StackPtr; }

  /// \brief SrcRC and DstRC will be morphed into NewRC if this returns true.
  bool shouldCoalesce(MachineInstr *MI,
                      const TargetRegisterClass *SrcRC,
                      unsigned SubReg,
                      const TargetRegisterClass *DstRC,
                      unsigned DstSubReg,
                      const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override;
};
} // End llvm namespace

#endif
