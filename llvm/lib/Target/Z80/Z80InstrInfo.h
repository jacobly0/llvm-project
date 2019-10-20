//===-- Z80InstrInfo.h - Z80 Instruction Information ------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Z80 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80INSTRINFO_H
#define LLVM_LIB_TARGET_Z80_Z80INSTRINFO_H

#include "Z80RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "Z80GenInstrInfo.inc"

namespace llvm {
class Z80Subtarget;

namespace Z80 {
  // Z80 specific condition code. These correspond to Z80_*_COND in
  // Z80InstrInfo.td. They must be kept in synch.
enum CondCode {
  COND_NZ = 0,
  COND_Z = 1,
  COND_NC = 2,
  COND_C = 3,
  LAST_SIMPLE_COND = COND_C,

  COND_PO = 4,
  COND_PE = 5,
  COND_P = 6,
  COND_M = 7,
  LAST_VALID_COND = COND_M,

  COND_INVALID
};

CondCode GetBranchConditionForPredicate(CmpInst::Predicate Pred, bool &IsSigned,
                                        bool &IsSwapped, bool &IsConst);

/// GetOppositeBranchCondition - Return the inverse of the specified cond,
/// e.g. turning COND_Z to COND_NZ.
CondCode GetOppositeBranchCondition(CondCode CC);

bool splitReg(unsigned ByteSize, unsigned Opc8, unsigned Opc16, unsigned Opc24,
              unsigned &RC, unsigned &LoOpc, unsigned &LoIdx, unsigned &HiOpc,
              unsigned &HiIdx, unsigned &HiOff, bool Has16BitEZ80Ops);
} // end namespace Z80;

namespace Z80II {
  enum {
    PrefixShift = 0,
    NoPrefix = 0 << PrefixShift,
    CBPrefix = 1 << PrefixShift,
    DDPrefix = 2 << PrefixShift,
    DDCBPrefix = 3 << PrefixShift,
    EDPrefix = 4 << PrefixShift,
    FDPrefix = 5 << PrefixShift,
    FDCBPrefix = 6 << PrefixShift,
    AnyIndexPrefix = 7 << PrefixShift,
    PrefixMask = 7 << PrefixShift,
    IndexedIndexPrefix = 8 << PrefixShift,

    ModeShift = 4,
    AnyMode = 0 << ModeShift,
    CurMode = 1 << ModeShift,
    Z80Mode = 2 << ModeShift,
    EZ80Mode = 3 << ModeShift,
    ModeMask = 3 << ModeShift,

    HasImm = 1 << 6,
    HasOff = 1 << 7,

    OpcodeShift = 8,
    OpcodeMask = 0xFF << OpcodeShift
  };
} // end namespace Z80II;

class Z80InstrInfo final : public Z80GenInstrInfo {
  Z80Subtarget &Subtarget;
  const Z80RegisterInfo RI;

  virtual void anchor();

public:
  explicit Z80InstrInfo(Z80Subtarget &STI);

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const Z80RegisterInfo &getRegisterInfo() const { return RI; }

  int getSPAdjust(const MachineInstr &MI) const override;

  int64_t getFrameAdjustment(const MachineInstr &MI) const {
    assert(isFrameInstr(MI) && "Not a frame instruction");
    int64_t Amount = getFrameSize(MI) -
                     MI.getOperand(MI.getNumExplicitOperands() - 1).getImm();
    return isFrameSetup(MI) ? -Amount : Amount;
  }

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  // Branch analysis.
  bool isUnpredicatedTerminator(const MachineInstr &MI) const override;
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DstReg, MCRegister SrcReg,
                   bool KillSrc = false) const override;
  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI,
                           unsigned SrcReg, bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI) const override;
  unsigned isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            unsigned DstReg, int FrameIndex,
                            const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI) const override;
  unsigned isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                         AAResults *AA) const override;
  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                     unsigned DstReg, unsigned SubIdx, const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  /// For a comparison instruction, return the source registers in SrcReg and
  /// SrcReg2 if having two register operands, and the value it compares against
  /// in CmpValue. Return true if the comparison instruction can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, unsigned &SrcReg,
                      unsigned &SrcReg2, int &CmpMask,
                      int &CmpValue) const override;
  /// Check if there exists an earlier instruction that operates on the same
  /// source operands and sets flags in the same way as Compare; remove Compare
  /// if possible.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, unsigned SrcReg,
                            unsigned SrcReg2, int CmpMask, int CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  /// Check whether the target can fold a load that feeds a subreg operand
  /// (or a subreg operand that feeds a store).
  bool isSubregFoldable() const override { return true; }

  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr,
                        VirtRegMap *VRM = nullptr) const override;
  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt,
                        MachineInstr &LoadMI,
                        LiveIntervals *LIS = nullptr) const override;

private:
  /// canExchange - This returns whether the two instructions can be directly
  /// exchanged with one EX instruction. Since the only register exchange
  /// instruction is EX DE,HL, simply returns whether the two arguments are
  /// super-registers of E and L, in any order.
  bool canExchange(unsigned RegA, unsigned RegB) const;

  /// isFrameOperand - Return true and the FrameIndex if the specified
  /// operand and follow operands form a reference to the stack frame.
  bool isFrameOperand(const MachineInstr &MI, unsigned int Op,
                      int &FrameIndex) const;

  void expandLoadStoreWord(const TargetRegisterClass *ARC, unsigned AOpc,
                           const TargetRegisterClass *ORC, unsigned OOpc,
                           MachineInstr &MI, unsigned RegIdx) const;
};

} // End llvm namespace

#endif
