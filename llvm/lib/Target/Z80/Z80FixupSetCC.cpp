//===---- Z80FixupSetCC.cpp - fixup usages of the flags register ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a pass that fixes uses of the flags register.
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "z80-fixup-setcc"

STATISTIC(NumFlagCopiesFixed, "Number of flag register usages removed");

namespace {
class Z80FixupSetCCPass : public MachineFunctionPass {
public:
  Z80FixupSetCCPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Z80 Fixup SetCC"; }

private:
  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;
};

char Z80FixupSetCCPass::ID = 0;
}

FunctionPass *llvm::createZ80FixupSetCC() { return new Z80FixupSetCCPass; }

bool Z80FixupSetCCPass::runOnMachineFunction(MachineFunction &MF) {
  if (!MF.getFunction().hasOptNone()) {
    LLVM_DEBUG(dbgs() << "Skipping flag register cleanup, "
                         "peephole should take care of it\n");
    return false;
  }

  bool Changed = false;
  MachineRegisterInfo &MRI = MF.getRegInfo();

  LLVM_DEBUG(dbgs() << "Cleaning up flag register usages\n");
  for (auto &MBB : MF) {
    auto I = MBB.begin(), E = MBB.end();
    while (I != E) {
      MachineInstr &MI = *I;
      ++I;

      if (MI.getOpcode() != TargetOpcode::COPY)
        continue;

      Register DstReg = MI.getOperand(0).getReg();
      Register SrcReg = MI.getOperand(1).getReg();

      if (SrcReg != Z80::F)
        continue;

      if (DstReg != Z80::F) {
        LLVM_DEBUG(dbgs() << "Replacing Def: "; MI.dump());
        MRI.replaceRegWith(DstReg, SrcReg);
      } else
        LLVM_DEBUG(dbgs() << "Removing Copy: "; MI.dump());

      MI.eraseFromParent();
      ++NumFlagCopiesFixed;
      Changed = true;
    }
  }

  return Changed;
}
