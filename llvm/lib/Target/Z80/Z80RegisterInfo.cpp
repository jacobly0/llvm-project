//===-- Z80RegisterInfo.cpp - Z80 Register Information --------------------===//
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

#include "Z80RegisterInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80FrameLowering.h"
#include "Z80MachineFunctionInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "z80reginfo"

#define GET_REGINFO_TARGET_DESC
#include "Z80GenRegisterInfo.inc"

Z80RegisterInfo::Z80RegisterInfo(const Triple &TT)
    : Z80GenRegisterInfo(0, 0, 0, Z80::PC) {
  // Cache some information.
  Is24Bit = !TT.isArch16Bit() && TT.getEnvironment() != Triple::CODE16;
  StackPtr = Is24Bit ? Z80::SPL : Z80::SPS;
}

unsigned Z80RegisterInfo::getSpillSize(const TargetRegisterClass &RC) const {
  unsigned SpillSize = TargetRegisterInfo::getSpillSize(RC);
  if (Is24Bit && SpillSize == 2)
    SpillSize = 3;
  return SpillSize;
}

const TargetRegisterClass *
Z80RegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                    unsigned Kind) const {
  switch (Kind) {
  default: llvm_unreachable("Unexpected Kind!");
  case 0: return Is24Bit ? &Z80::R24RegClass : &Z80::R16RegClass;
  case 1: return Is24Bit ? &Z80::G24RegClass : &Z80::G16RegClass;
  case 2: return Is24Bit ? &Z80::O24RegClass : &Z80::O16RegClass;
  case 3: return Is24Bit ? &Z80::A24RegClass : &Z80::A16RegClass;
  case 4: return Is24Bit ? &Z80::I24RegClass : &Z80::I16RegClass;
  }
}

const TargetRegisterClass *
Z80RegisterInfo::getPointerRegClassForConstraint(const MachineFunction &MF,
                                                 unsigned Constraint) const {
  unsigned Kind;
  switch (Constraint) {
  default: llvm_unreachable("Unexpected Constraint!");
  case InlineAsm::Constraint_V: Kind = 1; break;
  case InlineAsm::Constraint_m: Kind = 2; break;
  case InlineAsm::Constraint_o: Kind = 3; break;
  }
  return getPointerRegClass(MF, Kind);
}

const TargetRegisterClass *
Z80RegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                           const MachineFunction &) const {
  const TargetRegisterClass *Super = RC;
  TargetRegisterClass::sc_iterator I = RC->getSuperClasses();
  do {
    switch (Super->getID()) {
    case Z80::R8RegClassID:
    case Z80::R16RegClassID:
    case Z80::R24RegClassID:
      return Super;
    }
    Super = *I++;
  } while (Super);
  return RC;
}

unsigned Z80RegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                              MachineFunction &MF) const {
  return 3;

  switch (RC->getID()) {
  default:
    return 0;
  case Z80::R16RegClassID:
  case Z80::R24RegClassID:
    return 2;
  }
}

const MCPhysReg *
Z80RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const Z80Subtarget &STI = MF->getSubtarget<Z80Subtarget>();
  switch (MF->getFunction().getCallingConv()) {
  default:
    llvm_unreachable("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    if (STI.isZZ80())
      return CSR_ZZ80_C_SaveList;
    return Is24Bit ? CSR_EZ80_C_SaveList : CSR_Z80_C_SaveList;
  case CallingConv::Z80_LibCall:
  case CallingConv::Z80_LibCall_AB:
  case CallingConv::Z80_LibCall_AC:
  case CallingConv::Z80_LibCall_BC:
  case CallingConv::Z80_LibCall_L:
  case CallingConv::Z80_LibCall_F:
    return Is24Bit ? CSR_EZ80_AllRegs_SaveList : CSR_Z80_AllRegs_SaveList;
  case CallingConv::Z80_LibCall_16:
    return Is24Bit ? CSR_EZ80_AllRegs16_SaveList : CSR_Z80_AllRegs_SaveList;
  case CallingConv::PreserveAll:
    return Is24Bit ? CSR_EZ80_AllRegsAndFlags_SaveList
                   : CSR_Z80_AllRegsAndFlags_SaveList;
  case CallingConv::Z80_TIFlags:
    return Is24Bit ? CSR_EZ80_TIFlags_SaveList : CSR_Z80_TIFlags_SaveList;
  }
}

const uint32_t *
Z80RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  switch (CC) {
  default: llvm_unreachable("Unsupported calling convention");
  case CallingConv::C:
  case CallingConv::Fast:
    if (MF.getSubtarget<Z80Subtarget>().isZZ80())
      return CSR_ZZ80_C_RegMask;
    return Is24Bit ? CSR_EZ80_C_RegMask : CSR_Z80_C_RegMask;
  case CallingConv::PreserveAll:
  case CallingConv::Z80_LibCall:
  case CallingConv::Z80_LibCall_AB:
  case CallingConv::Z80_LibCall_AC:
  case CallingConv::Z80_LibCall_BC:
  case CallingConv::Z80_LibCall_L:
  case CallingConv::Z80_LibCall_F:
    return Is24Bit ? CSR_EZ80_AllRegs_RegMask : CSR_Z80_AllRegs_RegMask;
  case CallingConv::Z80_LibCall_16:
    return Is24Bit ? CSR_EZ80_AllRegs16_RegMask : CSR_Z80_AllRegs_RegMask;
  case CallingConv::Z80_TIFlags:
    return Is24Bit ? CSR_EZ80_TIFlags_RegMask : CSR_Z80_TIFlags_RegMask;
  }
}
const uint32_t *Z80RegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

BitVector Z80RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  // Set the stack-pointer registers as reserved.
  Reserved.set(Z80::SPS);
  Reserved.set(Z80::SPL);

  // Set the program-counter register as reserved.
  Reserved.set(getProgramCounter());

  // Set the frame-pointer register and its aliases as reserved if needed.
  // On ZZ80 without FP, IX and IY are allocatable (callee-saved).
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  if (!(STI.isZZ80() && !getFrameLowering(MF)->hasFP(MF))) {
    for (Register Reg :
         {Register(Is24Bit ? Z80::UIX : Z80::IX), getFrameRegister(MF)})
      for (MCRegAliasIterator I(Reg, this, /*IncludeSelf=*/true); I.isValid();
           ++I)
        Reserved.set(*I);
  }

  return Reserved;
}

bool Z80RegisterInfo::saveScavengerRegister(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MI,
                                            MachineBasicBlock::iterator &UseMI,
                                            const TargetRegisterClass *RC,
                                            Register Reg) const {
  return false;
  const Z80Subtarget &STI = MBB.getParent()->getSubtarget<Z80Subtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL;
  if (Reg == Z80::AF)
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF));
  else
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
        .addReg(Reg);
  for (MachineBasicBlock::iterator II = MI; II != UseMI; ++II) {
    if (II->isDebugValue())
      continue;
    if (II->modifiesRegister(Reg, this))
      UseMI = II;
  }
  if (Reg == Z80::AF)
    BuildMI(MBB, UseMI, DL, TII.get(Is24Bit ? Z80::POP24AF : Z80::POP16AF));
  else
    BuildMI(MBB, UseMI, DL, TII.get(Is24Bit ? Z80::POP24r : Z80::POP16r), Reg);
  return true;
}

void Z80RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  const Z80InstrInfo &TII = *STI.getInstrInfo();
  const Z80FrameLowering *TFI = getFrameLowering(MF);
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  auto Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) -
                TFI->getOffsetOfLocalArea();
  if (FrameIndex < 0)
    // For fixed indices, skip over callee save slots.
    Offset += MF.getInfo<Z80MachineFunctionInfo>()->getCalleeSavedFrameSize();

  if (STI.isZZ80() && !TFI->hasFP(MF)) {
    // SP-relative frame access: compute offset from current SP.
    Offset += MF.getFrameInfo().getStackSize() + SPAdj;
    int64_t InstrOffset =
        TII.getRegisterInfo().getFrameIndexInstrOffset(&MI, FIOperandNum);
    int64_t NewOffset = Offset + InstrOffset;

    DebugLoc DL = MI.getDebugLoc();
    unsigned Opc = MI.getOpcode();
    // Save iterator to the next instruction before modifying/erasing MI.
    MachineBasicBlock::iterator NextII = std::next(II);

    // Tier 1: Direct SP-relative instruction conversion.
    // Replace o-form instructions with native SP-relative instructions
    // that encode the offset directly, needing no scratch register.
    if (STI.hasSPRelative()) {
      unsigned SPOpc = 0;
      switch (Opc) {
      default: break;
      // 8-bit loads: LD r,(SP+dd) — any G8 register
      case Z80::LD8go:  SPOpc = Z80::LD8gs_sp;  break;
      case Z80::LD8ro:
        // LD8ro is a pseudo for R8; only G8 registers supported by SP-relative
        if (Z80::G8RegClass.contains(MI.getOperand(0).getReg()))
          SPOpc = Z80::LD8gs_sp;
        break;
      // 8-bit stores: only A can store to (SP+dd)
      case Z80::LD8og:
        if (MI.getOperand(FIOperandNum + 2).getReg() == Z80::A)
          SPOpc = Z80::LD8sa_sp;
        break;
      case Z80::LD8or:
        if (MI.getOperand(FIOperandNum + 2).getReg() == Z80::A)
          SPOpc = Z80::LD8sa_sp;
        break;
      // 8-bit store immediate: LD (SP+dd),n
      case Z80::LD8oi:  SPOpc = Z80::LD8si_sp;  break;
      // 16-bit loads: only HL can load from (SP+dd)
      case Z80::LD16ro:
      case Z80::LD88ro:
        if (MI.getOperand(0).getReg() == Z80::HL)
          SPOpc = Z80::LD16hs_sp;
        break;
      // 16-bit stores: only HL can store to (SP+dd)
      case Z80::LD16or:
      case Z80::LD88or:
        if (MI.getOperand(FIOperandNum + 2).getReg() == Z80::HL)
          SPOpc = Z80::LD16sh_sp;
        break;
      // LEA (load effective address) → LDA HL,(SP+dd)
      case Z80::LEA16ro:
        if (MI.getOperand(0).getReg() == Z80::HL)
          SPOpc = Z80::LDA16hs_sp;
        break;
      // 8-bit ALU with (SP+dd) operand
      case Z80::ADD8ao: SPOpc = Z80::ADD8as_sp; break;
      case Z80::ADC8ao: SPOpc = Z80::ADC8as_sp; break;
      case Z80::SUB8ao: SPOpc = Z80::SUB8as_sp; break;
      case Z80::SBC8ao: SPOpc = Z80::SBC8as_sp; break;
      case Z80::AND8ao: SPOpc = Z80::AND8as_sp; break;
      case Z80::XOR8ao: SPOpc = Z80::XOR8as_sp; break;
      case Z80::OR8ao:  SPOpc = Z80::OR8as_sp;  break;
      case Z80::CP8ao:  SPOpc = Z80::CP8as_sp;  break;
      // INC/DEC on (SP+dd)
      case Z80::INC8o:  SPOpc = Z80::INC8s_sp;  break;
      case Z80::DEC8o:  SPOpc = Z80::DEC8s_sp;  break;
      }

      if (SPOpc) {
        // Build the replacement SP-relative instruction.
        switch (Opc) {
        // 8-bit load: LD r,(SP+dd) — has explicit dst register
        case Z80::LD8go:
        case Z80::LD8ro: {
          Register DstReg = MI.getOperand(0).getReg();
          BuildMI(MBB, II, DL, TII.get(SPOpc), DstReg).addImm(NewOffset);
          break;
        }
        // 8-bit store A: LD (SP+dd),A — A is implicit
        case Z80::LD8og:
        case Z80::LD8or:
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset);
          break;
        // 8-bit store immediate: LD (SP+dd),n
        case Z80::LD8oi: {
          int64_t ImmVal = MI.getOperand(FIOperandNum + 2).getImm();
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset).addImm(ImmVal);
          break;
        }
        // 16-bit load HL: LDw HL,(SP+dd) — HL is implicit def
        case Z80::LD16ro:
        case Z80::LD88ro:
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset);
          break;
        // 16-bit store HL: LDw (SP+dd),HL — HL is implicit use
        case Z80::LD16or:
        case Z80::LD88or:
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset);
          break;
        // LEA → LDA HL,(SP+dd) — HL is implicit def
        case Z80::LEA16ro:
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset);
          break;
        // ALU, INC/DEC: all have just the offset operand
        default:
          BuildMI(MBB, II, DL, TII.get(SPOpc)).addImm(NewOffset);
          break;
        }
        MI.eraseFromParent();
        return;
      }
    }

    // Tier 2: Use LDA HL,(SP+dd) when available, otherwise LD HL,dd; ADD HL,SP.
    // LDA saves one instruction over the LD+ADD sequence.

    // Find an unused A16 register without spilling (avoids circular
    // dependency since spilling would create new frame index references).
    Register ScratchReg = RS ? RS->FindUnusedReg(&Z80::A16RegClass)
                             : Register();
    bool NeedSave = !ScratchReg;
    if (NeedSave) {
      ScratchReg = Z80::HL;
      // If the instruction defines ScratchReg (e.g., LEA with HL destination),
      // we don't need to save/restore it since it will be overwritten anyway.
      if (Opc == Z80::LEA16ro && MI.getOperand(0).getReg() == ScratchReg)
        NeedSave = false;
    }
    if (NeedSave) {
      // No free A16 register — use HL and save/restore via PUSH/POP.
      // PUSH changes SP, so adjust the offset to compensate.
      NewOffset += TFI->getSlotSize();
      TII.applySPAdjust(
          *BuildMI(MBB, II, DL, TII.get(Z80::PUSH16r))
               .addReg(ScratchReg)
               .setMIFlag(MachineInstr::FrameSetup));
    }

    // Materialize effective address into ScratchReg.
    if (STI.hasSPRelative() && ScratchReg == Z80::HL) {
      // LDA HL,(SP+offset) — single instruction
      BuildMI(MBB, II, DL, TII.get(Z80::LDA16hs_sp)).addImm(NewOffset);
    } else {
      // LD scratch, offset; ADD scratch, SP — two instructions
      BuildMI(MBB, II, DL, TII.get(Z80::LD16ri), ScratchReg).addImm(NewOffset);
      BuildMI(MBB, II, DL, TII.get(Z80::ADD16as), ScratchReg)
          .addReg(ScratchReg)
          ->addRegisterDead(Z80::F, this);
    }

    // Convert the instruction to use pointer-indirect through ScratchReg.
    if (Z80::I16RegClass.contains(ScratchReg)) {
      // IX or IY: keep the offset-form with offset 0.
      MI.getOperand(FIOperandNum).ChangeToRegister(ScratchReg, false);
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
    } else if (Opc == Z80::LEA16ro) {
      // LEA is "load effective address" — the address is already in
      // ScratchReg, just copy it to the destination if different.
      Register DstReg = MI.getOperand(0).getReg();
      MI.eraseFromParent();
      if (DstReg != ScratchReg)
        TII.copyRegister(MBB, NextII, DL, DstReg, ScratchReg);
    } else {
      // HL (or other non-index): convert from offset-form (o) to
      // pointer-form (p).
      MI.getOperand(FIOperandNum).ChangeToRegister(ScratchReg, false);
      unsigned NewOpc;
      switch (Opc) {
      default: llvm_unreachable("Unexpected opcode for SP-relative rewrite!");
      case Z80::LD8ro:   NewOpc = Z80::LD8rp;   break;
      case Z80::LD8go:   NewOpc = Z80::LD8gp;   break;
      case Z80::LD16ro:  NewOpc = Z80::LD16rp;  break;
      case Z80::LD88ro:  NewOpc = Z80::LD88rp;  break;
      case Z80::LD8or:   NewOpc = Z80::LD8pr;   break;
      case Z80::LD8og:   NewOpc = Z80::LD8pg;   break;
      case Z80::LD16or:  NewOpc = Z80::LD16pr;  break;
      case Z80::LD88or:  NewOpc = Z80::LD88pr;  break;
      case Z80::LD8oi:   NewOpc = Z80::LD8pi;   break;
      case Z80::PEA16o:  NewOpc = Z80::PUSH16r; break;
      case Z80::RLC8o:   NewOpc = Z80::RLC8p;   break;
      case Z80::RRC8o:   NewOpc = Z80::RRC8p;   break;
      case Z80::RL8o:    NewOpc = Z80::RL8p;    break;
      case Z80::RR8o:    NewOpc = Z80::RR8p;    break;
      case Z80::SLA8o:   NewOpc = Z80::SLA8p;   break;
      case Z80::SRA8o:   NewOpc = Z80::SRA8p;   break;
      case Z80::SRL8o:   NewOpc = Z80::SRL8p;   break;
      case Z80::BIT8ob:  NewOpc = Z80::BIT8pb;  break;
      case Z80::RES8ob:  NewOpc = Z80::RES8pb;  break;
      case Z80::SET8ob:  NewOpc = Z80::SET8pb;  break;
      case Z80::INC8o:   NewOpc = Z80::INC8p;   break;
      case Z80::DEC8o:   NewOpc = Z80::DEC8p;   break;
      case Z80::ADD8ao:  NewOpc = Z80::ADD8ap;  break;
      case Z80::ADC8ao:  NewOpc = Z80::ADC8ap;  break;
      case Z80::SUB8ao:  NewOpc = Z80::SUB8ap;  break;
      case Z80::SBC8ao:  NewOpc = Z80::SBC8ap;  break;
      case Z80::AND8ao:  NewOpc = Z80::AND8ap;  break;
      case Z80::XOR8ao:  NewOpc = Z80::XOR8ap;  break;
      case Z80::OR8ao:   NewOpc = Z80::OR8ap;   break;
      case Z80::CP8ao:   NewOpc = Z80::CP8ap;   break;
      }
      MI.setDesc(TII.get(NewOpc));
      MI.removeOperand(FIOperandNum + 1);
    }

    if (NeedSave) {
      // Restore the saved register. Insert POP after all modified/inserted
      // instructions, right before the original next instruction.
      TII.applySPAdjust(
          *BuildMI(MBB, NextII, DL, TII.get(Z80::POP16r), ScratchReg)
               .setMIFlag(MachineInstr::FrameDestroy));
    }
    return;
  }

  assert(TFI->hasFP(MF) && "Stack slot use without fp unimplemented");
  Register BaseReg = getFrameRegister(MF);
  TII.rewriteFrameIndex(MI, FIOperandNum, BaseReg, Offset, RS, SPAdj);
}

Register Z80RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return getFrameLowering(MF)->hasFP(MF)
             ? MF.getInfo<Z80MachineFunctionInfo>()->getUsesAltFP()
                   ? Is24Bit ? Z80::UIY : Z80::IY
                   : Is24Bit ? Z80::UIX : Z80::IX
             : Is24Bit ? Z80::SPL : Z80::SPS;
}

bool Z80RegisterInfo::
shouldCoalesce(MachineInstr *MI,
               const TargetRegisterClass *SrcRC, unsigned SrcSubReg,
               const TargetRegisterClass *DstRC, unsigned DstSubReg,
               const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  LLVM_DEBUG(
      dbgs() << getRegClassName(SrcRC) << '[' << SrcRC->getNumRegs()
             << "]:" << (SrcSubReg ? getSubRegIndexName(SrcSubReg) : "")
             << " -> " << getRegClassName(DstRC) << '[' << DstRC->getNumRegs()
             << "]:" << (DstSubReg ? getSubRegIndexName(DstSubReg) : "") << ' '
             << getRegClassName(NewRC) << '[' << NewRC->getNumRegs() << "]\n");
  // Don't coalesce if SrcRC and DstRC have a small intersection.
  return std::min(SrcRC->getNumRegs(), DstRC->getNumRegs()) <=
         NewRC->getNumRegs();
}

bool Z80RegisterInfo::requiresVirtualBaseRegisters(
    const MachineFunction &MF) const {
  return true;
}
int64_t Z80RegisterInfo::getFrameIndexInstrOffset(const MachineInstr *MI,
                                                  int FIOperandNum) const {
  return MI->getOperand(FIOperandNum + 1).getImm();
}
bool Z80RegisterInfo::needsFrameBaseReg(MachineInstr *MI,
                                        int64_t Offset) const {
  return !isFrameOffsetLegal(MI, getFrameRegister(*MI->getMF()), Offset);
}
Register Z80RegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                       int FrameIdx,
                                                       int64_t Offset) const {
  MachineFunction &MF = *MBB->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  const Z80InstrInfo &TII = *STI.getInstrInfo();
  MachineBasicBlock::iterator II = MBB->begin();
  DebugLoc DL = MBB->findDebugLoc(II);
  const MCInstrDesc &MCID = TII.get(Is24Bit ? Z80::LEA24ro : Z80::LEA16ro);
  Register BaseReg = MRI.createVirtualRegister(TII.getRegClass(MCID, 0, this, MF));
  BuildMI(*MBB, II, DL, MCID, BaseReg).addFrameIndex(FrameIdx).addImm(Offset);
  return BaseReg;
}

static bool isSplitLoadStoreOpc(unsigned Opc) {
  switch (Opc) {
  default:
    return false;
  case Z80::LD88rp:
  case Z80::LD88ro:
  case Z80::LD88pr:
  case Z80::LD88or:
    return true;
  }
}
static unsigned getFIOperandNum(const MachineInstr &MI) {
  for (const auto &MO : MI.explicit_uses())
    if (MO.isFI())
      return MI.getOperandNo(&MO);
  llvm_unreachable("Instr doesn't have a FrameIndex operand!");
}

void Z80RegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                        int64_t Offset) const {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  const Z80InstrInfo &TII = *STI.getInstrInfo();

  MRI.constrainRegClass(BaseReg,
                        Is24Bit ? &Z80::I24RegClass : &Z80::I16RegClass);
  TII.rewriteFrameIndex(MI, getFIOperandNum(MI), BaseReg, Offset);
}
bool Z80RegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                         Register BaseReg,
                                         int64_t Offset) const {
  Offset += getFrameIndexInstrOffset(MI, getFIOperandNum(*MI));
  return isInt<8>(Offset) &&
         (!isSplitLoadStoreOpc(MI->getOpcode()) || isInt<8>(Offset + 1));
}
