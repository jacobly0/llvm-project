//=== lib/CodeGen/GlobalISel/Z80PostSelectCombiner.cpp --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-postselect-combiner"

using namespace llvm;

namespace {
class Z80PostSelectCombiner : public MachineFunctionPass {
public:
  static char ID;

  Z80PostSelectCombiner();

  StringRef getPassName() const override { return "Z80PostSelectCombiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};
} // end anonymous namespace

void Z80PostSelectCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  MachineFunctionPass::getAnalysisUsage(AU);
}

Z80PostSelectCombiner::Z80PostSelectCombiner() : MachineFunctionPass(ID) {
  initializeZ80PostSelectCombinerPass(*PassRegistry::getPassRegistry());
}

bool Z80PostSelectCombiner::runOnMachineFunction(MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  const Z80InstrInfo &TII = *STI.getInstrInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    auto I = MBB.begin(), E = MBB.end();
    while (I != E) {
      MachineInstr &MI = *I;
      ++I;

      switch (unsigned Opc = MI.getOpcode()) {
      case Z80::LEA16ro:
      case Z80::LEA24ro: {
        bool IsLea24 = Opc == Z80::LEA24ro;
        if (!MI.getOperand(1).isReg())
          break;
        Register BaseReg = MI.getOperand(1).getReg();
        if (!MRI.hasOneUse(BaseReg))
          break;
        MachineInstr *BaseMI = MRI.getVRegDef(BaseReg);
        if (!BaseMI ||
            BaseMI->getOpcode() != (IsLea24 ? Z80::LEA24ro : Z80::LEA16ro))
          break;
        auto NewOff =
            BaseMI->getOperand(2).getImm() + MI.getOperand(2).getImm();
        if (!isInt<8>(NewOff))
          break;
        MI.RemoveOperand(2);
        MI.RemoveOperand(1);
        MachineInstrBuilder(MF, MI).add(BaseMI->getOperand(1)).addImm(NewOff);
        BaseMI->eraseFromParent();
        Changed = true;
        break;
      }
      case Z80::PUSH16r:
      case Z80::PUSH24r: {
        bool IsPush24 = Opc == Z80::PUSH24r;
        Register SrcReg = MI.getOperand(0).getReg();
        if (!MRI.hasOneUse(SrcReg))
          break;
        MachineInstr *SrcMI = MRI.getVRegDef(SrcReg);
        if (!SrcMI ||
            SrcMI->getOpcode() != (IsPush24 ? Z80::LEA24ro : Z80::LEA16ro))
          break;
        MachineOperand &BaseMO = SrcMI->getOperand(1);
        auto NewOff = SrcMI->getOperand(2).getImm();
        if (!BaseMO.isReg() || NewOff) {
          MI.RemoveOperand(0);
          MI.setDesc(TII.get(IsPush24 ? Z80::PEA24o : Z80::PEA16o));
          MachineInstrBuilder(MF, MI).add(SrcMI->getOperand(1)).addImm(NewOff);
        } else
          MI.getOperand(0).setReg(BaseMO.getReg());
        SrcMI->eraseFromParent();
        Changed = true;
        break;
      }
      default:
        break;
      }
    }
  }

  return Changed;
}

char Z80PostSelectCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PostSelectCombiner, DEBUG_TYPE,
                      "Combine Z80 machine instrs after inst selection", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig);
INITIALIZE_PASS_DEPENDENCY(InstructionSelect);
INITIALIZE_PASS_END(Z80PostSelectCombiner, DEBUG_TYPE,
                    "Combine Z80 machine instrs after inst selection", false,
                    false)


namespace llvm {
FunctionPass *createZ80PostSelectCombiner() {
  return new Z80PostSelectCombiner;
}
} // end namespace llvm
