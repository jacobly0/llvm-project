//===-- Z80MachineFunctionInfo.h - Z80 machine function info ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares Z80-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_Z80_Z80MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// Z80MachineFunctionInfo - This class is derived from MachineFunction and
/// contains private Z80 target-specific information for each MachineFunction.
class Z80MachineFunctionInfo : public MachineFunctionInfo {
  /// CalleeSavedFrameSize - Size of the callee-saved register portion of the
  /// stack frame in bytes.
  unsigned CalleeSavedFrameSize = 0;

  /// VarArgsFrameIndex - FrameIndex for start of varargs area.
  int VarArgsFrameIndex = 0;

  /// SRetReturnReg - Some subtargets require that sret lowering includes
  /// returning the value of the returned struct in a register. This field
  /// holds the virtual register into which the sret argument is passed.
  Register SRetReturnReg = 0;

  /// HasIllegalLEA - We use LEA to materialize a frame index address even if it
  /// is illegal.  Remember when if we do, to ensure a scavenging frame index is
  /// created.
  bool HasIllegalLEA = false;

public:
  Z80MachineFunctionInfo() = default;

  explicit Z80MachineFunctionInfo(MachineFunction &MF) {}

  unsigned getCalleeSavedFrameSize() const { return CalleeSavedFrameSize; }
  void setCalleeSavedFrameSize(unsigned Bytes) { CalleeSavedFrameSize = Bytes; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Idx) { VarArgsFrameIndex = Idx; }

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  bool getHasIllegalLEA() const { return HasIllegalLEA; }
  void setHasIllegalLEA(bool V = true) { HasIllegalLEA = V; }
};

} // End llvm namespace

#endif
