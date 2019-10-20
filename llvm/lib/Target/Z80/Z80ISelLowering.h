//===-- Z80ISelLowering.h - Z80 DAG Lowering Interface ----------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Z80 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80ISELLOWERING_H
#define LLVM_LIB_TARGET_Z80_Z80ISELLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {
class Z80Subtarget;
class Z80TargetMachine;

namespace Z80ISD {
// Z80 Specific DAG Nodes
enum NodeType : unsigned {
  // Start the numbering where the builtin ops leave off.
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  /// A wrapper node for TargetConstantPool, TargetExternalSymbol, and
  /// TargetGlobalAddress.
  Wrapper,

  /// Shift/Rotate
  RLC, RRC, RL, RR, SLA, SRA, SRL,

  /// Arithmetic operation with flags results.
  INC, DEC, ADD, ADC, SUB, SBC, AND, XOR, OR,

  /// Z80 compare and test
  CP, TST,

  MLT,

  /// This produces an all zeros/ones value from an input carry (SBC r,r).
  SEXT,

  /// This operation represents an abstract Z80 call instruction, which
  /// includes a bunch of information.
  CALL,

  /// Return with a flag operand. Operand 0 is the chain operand, operand
  /// 1 is the number of bytes of stack to pop.
  RET_FLAG,

  /// Return from interrupt.
  RETN_FLAG, RETI_FLAG,

  /// Tail call return.
  TC_RETURN,

  /// BRCOND - Z80 conditional branch.  The first operand is the chain, the
  /// second is the block to branch to if the condition is true, the third is
  /// the condition, and the fourth is the flag operand.
  BRCOND,

  /// SELECT - Z80 select - This selects between a true value and a false
  /// value (ops #1 and #2) based on the condition in op #0 and flag in op #3.
  SELECT,

  /// Stack operations
  POP = ISD::FIRST_TARGET_MEMORY_OPCODE, PUSH
};
} // end Z80ISD namespace

//===----------------------------------------------------------------------===//
//  Z80 Implementation of the TargetLowering interface
class Z80TargetLowering final : public TargetLowering {
  /// Keep a reference to the Z80Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const Z80Subtarget &Subtarget;

  void setLibcall(RTLIB::Libcall Call, const char *Name, CallingConv::ID CC);

public:
  Z80TargetLowering(const Z80TargetMachine &TM, const Z80Subtarget &STI);

private:
  unsigned getJumpTableEncoding() const override;

  /// Return true if the target has native support for
  /// the specified value type and it is 'desirable' to use the type for the
  /// given node type. e.g. On ez80 i16 is legal, but undesirable since i16
  /// instruction encodings are longer and slower.
  bool isTypeDesirableForOp(unsigned Opc, EVT VT) const override;

  /// Return true if x op y -> (SrcVT)((DstVT)x op (DstVT)y) is beneficial.
  bool isDesirableToShrinkOp(unsigned Opc, EVT SrcVT, EVT DstVT) const;

  bool IsDesirableToPromoteOp(SDValue Op, EVT &PVT) const override;

  MachineBasicBlock *
    EmitInstrWithCustomInserter(MachineInstr &MI,
                                MachineBasicBlock *BB) const override;
  void AdjustInstrPostInstrSelection(MachineInstr &MI,
                                     SDNode *Node) const override;

  void AdjustAdjCallStack(MachineInstr &MI) const;
  MachineBasicBlock *EmitLoweredSub0(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredSub(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredCmp0(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredCmp(MachineInstr &MI,
                                    MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredSelect(MachineInstr &MI,
                                       MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredSExt(MachineInstr &MI,
                                     MachineBasicBlock *BB) const;
  MachineBasicBlock *EmitLoweredMemMove(MachineInstr &MI,
                                        MachineBasicBlock *BB) const;

  /// HandleByVal - Target-specific cleanup for ByVal support.
  void HandleByVal(CCState *, unsigned &, unsigned) const override;
};
} // End llvm namespace

#endif
