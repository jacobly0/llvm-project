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

  StringRef getPassName() const override { return "Z80 Post Select Combiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

class ValLoc {
  Register Reg, Base;
  int8_t Off;

  static Register getBaseReg(const MachineInstr &MI, unsigned OpNo) {
    Register BaseReg;
    const MachineOperand &BaseMO = MI.getOperand(OpNo);
    if (BaseMO.isReg())
      BaseReg = BaseMO.getReg();
    else if (BaseMO.getIndex() >= 0)
      return Register::index2StackSlot(BaseMO.getIndex());
    return BaseReg;
  }

public:
  ValLoc() : Reg(), Base(), Off() {}

  ValLoc &setReg(Register Reg) {
    this->Reg = Reg;
    return *this;
  }
  ValLoc &setReg(const MachineInstr &MI, unsigned OpNo) {
    return setReg(MI.getOperand(OpNo).getReg());
  }
  ValLoc &setMem(Register Base, int8_t Off = 0) {
    this->Base = Base;
    this->Off = Off;
    return *this;
  }
  ValLoc &setPtr(const MachineInstr &MI, unsigned OpNo) {
    return setMem(MI.getOperand(OpNo).getReg());
  }
  ValLoc &setOff(const MachineInstr &MI, unsigned OpNo) {
    return setMem(getBaseReg(MI, OpNo), MI.getOperand(OpNo + 1).getImm());
  }

  bool matchesReg(Register Reg) const {
    return Reg.isValid() && this->Reg == Reg;
  }
  bool matchesReg(const MachineInstr &MI, unsigned OpNo) const {
    return matchesReg(MI.getOperand(OpNo).getReg());
  }
  bool matchesMem(Register Base, int8_t Off = 0) const {
    return Base.isValid() && this->Base == Base && this->Off == Off;
  }
  bool matchesPtr(const MachineInstr &MI, unsigned OpNo) const {
    return matchesMem(MI.getOperand(OpNo).getReg());
  }
  bool matchesOff(const MachineInstr &MI, unsigned OpNo) const {
    return matchesMem(getBaseReg(MI, OpNo), MI.getOperand(OpNo + 1).getImm());
  }

  void clobberDefs(const MachineInstr &MI, const TargetRegisterInfo &TRI) {
    for (const MachineOperand &DefMO : MI.defs())
      for (Register LocReg : {Reg, Base})
        if (LocReg.isValid() && TRI.regsOverlap(DefMO.getReg(), LocReg))
          return clear();
  }

  void clear() {
    *this = ValLoc();
  }
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
  auto &STI = MF.getSubtarget<Z80Subtarget>();
  auto &TII = *STI.getInstrInfo();
  auto &TRI = *STI.getRegisterInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    auto I = MBB.begin(), E = MBB.end();
    ValLoc SZFlagLoc;
    auto FlagLocs = {&SZFlagLoc};
    while (I != E) {
      MachineInstr &MI = *I;
      ++I;

      switch (unsigned Opc = MI.getOpcode()) {
      case TargetOpcode::COPY: {
        for (ValLoc *FlagLoc : FlagLocs)
          if (FlagLoc->matchesReg(MI, 1))
            FlagLoc->setReg(MI, 0);
        Register DstReg = MI.getOperand(0).getReg();
        if (DstReg != Z80::SPS && DstReg != Z80::SPL)
          break;
        Register TmpReg =
            MRI.createVirtualRegister(DstReg == Z80::SPL ? &Z80::A24RegClass
                                                         : &Z80::A16RegClass);
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(TargetOpcode::COPY), TmpReg)
            .add(MI.getOperand(1));
        MI.setDesc(TII.get(DstReg == Z80::SPL ? Z80::LD24sa : Z80::LD16sa));
        MI.removeOperand(0);
        MI.getOperand(0).setReg(TmpReg);
        MachineInstrBuilder(MF, MI).addReg(DstReg, RegState::ImplicitDefine);
        Changed = true;
        break;
      }
      case Z80::PUSH16r:
      case Z80::PUSH24r: {
        if (!STI.hasEZ80Ops())
          break;
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
          MI.removeOperand(0);
          MI.setDesc(TII.get(IsPush24 ? Z80::PEA24o : Z80::PEA16o));
          MachineInstrBuilder(MF, MI).add(SrcMI->getOperand(1)).addImm(NewOff);
        } else
          MI.getOperand(0).setReg(BaseMO.getReg());
        SrcMI->eraseFromParent();
        Changed = true;
        break;
      }
      case Z80::LD8rp:
      case Z80::LD8gp:
        for (ValLoc *FlagLoc : FlagLocs)
          if (FlagLoc->matchesPtr(MI, 1))
            FlagLoc->setReg(MI, 0);
        break;
      case Z80::LD8ro:
      case Z80::LD8go:
        for (ValLoc *FlagLoc : FlagLocs)
          if (FlagLoc->matchesOff(MI, 1))
            FlagLoc->setReg(MI, 0);
        break;
      case Z80::LD8pr:
      case Z80::LD8pg:
        for (ValLoc *FlagLoc : FlagLocs)
          if (FlagLoc->matchesReg(MI, 1))
            FlagLoc->setPtr(MI, 0);
        break;
      case Z80::LD8or:
      case Z80::LD8og:
        for (ValLoc *FlagLoc : FlagLocs)
          if (FlagLoc->matchesReg(MI, 2))
            FlagLoc->setOff(MI, 0);
        break;
      case Z80::OR8ar:
        if (MI.getOperand(0).getReg() == Z80::A &&
            SZFlagLoc.matchesReg(MI, 0)) {
          MI.eraseFromParent();
          break;
        }
        LLVM_FALLTHROUGH;
      case Z80::ADD8ar:
      case Z80::ADD8ai:
      case Z80::ADC8ar:
      case Z80::ADC8ai:
      case Z80::SUB8ar:
      case Z80::SUB8ai:
      case Z80::SBC8ar:
      case Z80::SBC8ai:
      case Z80::AND8ar:
      case Z80::AND8ai:
      case Z80::XOR8ar:
      case Z80::XOR8ai:
      case Z80::OR8ai:
        SZFlagLoc.setReg(Z80::A);
        break;
      case Z80::RLC8g:
      case Z80::RRC8g:
      case Z80:: RL8g:
      case Z80:: RR8g:
      case Z80::SLA8g:
      case Z80::SRA8g:
      case Z80::SRL8g:
      case Z80::INC8r:
      case Z80::DEC8r:
        SZFlagLoc.setReg(MI, 0);
        break;
      case Z80::ADD8ap:
      case Z80::ADC8ap:
      case Z80::SUB8ap:
      case Z80::SBC8ap:
      case Z80::AND8ap:
      case Z80::XOR8ap:
      case Z80:: OR8ap:
      case Z80:: RLC8p:
      case Z80:: RRC8p:
      case Z80::  RL8p:
      case Z80::  RR8p:
      case Z80:: SLA8p:
      case Z80:: SRA8p:
      case Z80:: SRL8p:
      case Z80:: INC8p:
      case Z80:: DEC8p:
        SZFlagLoc.setPtr(MI, 0);
        break;
      case Z80::ADD8ao:
      case Z80::ADC8ao:
      case Z80::SUB8ao:
      case Z80::SBC8ao:
      case Z80::AND8ao:
      case Z80::XOR8ao:
      case Z80:: OR8ao:
      case Z80:: RLC8o:
      case Z80:: RRC8o:
      case Z80::  RL8o:
      case Z80::  RR8o:
      case Z80:: SLA8o:
      case Z80:: SRA8o:
      case Z80:: SRL8o:
      case Z80:: INC8o:
      case Z80:: DEC8o:
        SZFlagLoc.setOff(MI, 0);
        break;
      default:
        if (MI.modifiesRegister(Z80::F, &TRI))
          for (ValLoc *FlagLoc : FlagLocs)
            FlagLoc->clear();
        break;
      }

      for (ValLoc *FlagLoc : FlagLocs)
        FlagLoc->clobberDefs(MI, TRI);
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


FunctionPass *llvm::createZ80PostSelectCombiner() {
  return new Z80PostSelectCombiner;
}
