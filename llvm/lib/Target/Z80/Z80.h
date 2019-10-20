//===-- Z80.h - Top-level interface for Z80 representation ------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// Z80 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_Z80_Z80_H
#define LLVM_LIB_TARGET_Z80_Z80_H

namespace llvm {

class FunctionPass;
class InstructionSelector;
class Z80RegisterBankInfo;
class Z80Subtarget;
class Z80TargetMachine;

InstructionSelector *createZ80InstructionSelector(const Z80TargetMachine &TM,
                                                  Z80Subtarget &,
                                                  Z80RegisterBankInfo &);

void initializeZ80PreLegalizerCombinerPass(PassRegistry &);
void initializeZ80PostSelectCombinerPass(PassRegistry &);

} // namespace llvm

#endif
