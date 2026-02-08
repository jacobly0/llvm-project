//===-- Z80FrameLowering.cpp - Z80 Frame Information ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the z80 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "Z80FrameLowering.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "Z80MachineFunctionInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/MC/MCDwarf.h"
using namespace llvm;

#define DEBUG_TYPE "z80-frame-lowering"

Z80FrameLowering::Z80FrameLowering(const Z80Subtarget &STI)
    : TargetFrameLowering(StackGrowsDown, Align(), STI.is24Bit() ? -3 : -2),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()),
      Is24Bit(STI.is24Bit()), SlotSize(Is24Bit ? 3 : 2) {}

/// hasFP - Return true if the specified function should have a dedicated frame
/// pointer register.  This is true if the function has variable sized allocas
/// or if frame pointer elimination is disabled.
bool Z80FrameLowering::hasFP(const MachineFunction &MF) const {
  if (STI.isZZ80()) {
    // On ZZ80, we can use SP-relative addressing, so only need FP for:
    // - Variable-sized stack objects (alloca)
    // - Explicitly disabled frame pointer elimination
    return MF.getTarget().Options.DisableFramePointerElim(MF) ||
           MF.getFrameInfo().hasVarSizedObjects();
  }
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MF.getFrameInfo().hasStackObjects();
}
bool Z80FrameLowering::isFPSaved(const MachineFunction &MF) const {
  return hasFP(MF) && MF.getInfo<Z80MachineFunctionInfo>()->getUsesAltFP() ==
                          Z80MachineFunctionInfo::AFPM_None;
}
bool Z80FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return false; // call frames are not implemented yet
}
bool Z80FrameLowering::needsFrameIndexResolution(
    const MachineFunction &MF) const {
  return TargetFrameLowering::needsFrameIndexResolution(MF) ||
         !hasReservedCallFrame(MF);
}

Z80FrameLowering::StackAdjustmentMethod
Z80FrameLowering::getOptimalStackAdjustmentMethod(MachineFunction &MF,
                                                  int Offset, int FPOffset,
                                                  bool ScratchIsIndex,
                                                  bool UnknownOffset) const {
  if (!Offset)
    return SAM_None;

  if (MF.getFunction().hasOptNone())
    return SAM_Large;

  bool OptSize = MF.getFunction().hasOptSize();
  bool HasEZ80Ops = STI.hasEZ80Ops();

  // Optimal for small offsets
  //   POP/PUSH <scratch> for every SlotSize bytes
  unsigned PopPushCount = std::abs(Offset) / SlotSize;
  unsigned PopPushCost = OptSize ? 1 : HasEZ80Ops ? 4 : Offset > 0 ? 10 : 11;
  if (ScratchIsIndex)
    PopPushCost += OptSize || HasEZ80Ops ? 1 : 4;
  //   INC/DEC SP for remaining bytes
  unsigned IncDecCount = std::abs(Offset) % SlotSize;
  unsigned IncDecCost = OptSize || HasEZ80Ops ? 1 : 6;

  StackAdjustmentMethod BestMethod = PopPushCount ? SAM_Small : SAM_Tiny;
  unsigned BestCost = PopPushCount * PopPushCost + IncDecCount * IncDecCost;

  if (isFPSaved(MF)) {
    if (UnknownOffset || (FPOffset >= 0 && FPOffset == Offset)) {
      // Optimal if we are trying to set SP = FP, except for tiny Offset
      unsigned AllCost = 0;
      //   LD SP, FP
      AllCost += OptSize || HasEZ80Ops ? 2 : 10;

      return AllCost <= BestCost ? SAM_All : BestMethod;
    }

    if (HasEZ80Ops && FPOffset >= 0 && isInt<8>(Offset - FPOffset)) {
      // Optimal for medium offsets
      unsigned MediumCost = 0;
      //   LEA <scratch>, FP - Offset - FPOffset
      MediumCost += 3;
      //   LD SP, <scratch>
      MediumCost += ScratchIsIndex ? 2 : 1;

      if (MediumCost < BestCost) {
        BestMethod = SAM_Medium;
        BestCost = MediumCost;
      }
    }
  }

  // Optimal for large offsets
  unsigned LargeCost = 0;
  if (!Is24Bit && STI.hasLDA() && !ScratchIsIndex) {
    //   LDA HL,(SP+Offset) — replaces LD+ADD
    LargeCost += OptSize ? 4 : 10;
  } else {
    //   LD <scratch>, Offset
    LargeCost += OptSize || HasEZ80Ops ? 1 + SlotSize : 10;
    //   ADD <scratch>, SP
    LargeCost += OptSize || HasEZ80Ops ? 1 : 11;
    if (ScratchIsIndex)
      LargeCost += OptSize || HasEZ80Ops ? 3 : 12;
  }
  //   LD SP, <scratch>
  LargeCost += OptSize || HasEZ80Ops ? 1 : 6;

  return LargeCost < BestCost ? SAM_Large : BestMethod;
}

void Z80FrameLowering::BuildStackAdjustment(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, const DebugLoc &DL, Register ScratchReg,
    int Offset, int FPOffset, MachineInstr::MIFlag Flag,
    bool UnknownOffset) const {
  Register FrameReg = TRI->getFrameRegister(MF), ResultReg;
  bool ScratchIsIndex = Z80::I24RegClass.contains(ScratchReg) ||
                        Z80::I16RegClass.contains(ScratchReg);
  switch (getOptimalStackAdjustmentMethod(MF, Offset, FPOffset, ScratchIsIndex,
                                          UnknownOffset)) {
  case SAM_None:
    // Nothing to do
    return;
  case SAM_Small:
    for (unsigned PopPushCount = std::abs(Offset) / SlotSize; PopPushCount;
         --PopPushCount)
      TII.applySPAdjust(
          *BuildMI(MBB, MBBI, DL,
                   TII.get(Offset >= 0
                               ? (Is24Bit ? Z80::POP24r : Z80::POP16r)
                               : (Is24Bit ? Z80::PUSH24r : Z80::PUSH16r)))
               .addReg(ScratchReg, getDefRegState(Offset >= 0) |
                                       getDeadRegState(Offset >= 0) |
                                       getUndefRegState(Offset < 0))
               .setMIFlag(Flag));
    LLVM_FALLTHROUGH;
  case SAM_Tiny:
    for (unsigned IncDecCount = std::abs(Offset) % SlotSize; IncDecCount;
         --IncDecCount)
      TII.applySPAdjust(
          *BuildMI(MBB, MBBI, DL,
                   TII.get(Offset >= 0 ? (Is24Bit ? Z80::INC24s : Z80::INC16s)
                                       : (Is24Bit ? Z80::DEC24s : Z80::DEC16s)))
               .setMIFlag(Flag));
    return;
  case SAM_All:
    // Just store FP to SP
    ResultReg = FrameReg;
    break;
  case SAM_Medium:
    BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::LEA24ro : Z80::LEA16ro),
            ScratchReg)
        .addReg(FrameReg)
        .addImm(Offset - FPOffset)
        .setMIFlag(Flag);
    ResultReg = ScratchReg;
    break;
  case SAM_Large:
    if (!Is24Bit && STI.hasLDA() && ScratchReg == Z80::HL) {
      // LDA HL,(SP+Offset) — single instruction replaces LD+ADD
      BuildMI(MBB, MBBI, DL, TII.get(Z80::LDA16hs_sp))
          .addImm(Offset)
          .setMIFlag(Flag);
    } else {
      BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri),
              ScratchReg)
          .addImm(Offset)
          .setMIFlag(Flag);
      BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::ADD24as : Z80::ADD16as),
              ScratchReg)
          .addReg(ScratchReg)
          .setMIFlag(Flag)
          ->addRegisterDead(Z80::F, TRI);
    }
    ResultReg = ScratchReg;
    break;
  }

  BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::LD24sa : Z80::LD16sa))
      .addReg(ResultReg, RegState::Kill)
      .setMIFlag(Flag);
  if (MF.needsFrameMoves() && !hasFP(MF))
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(MF.addFrameInst(
            MCCFIInstruction::createAdjustCfaOffset(nullptr, Offset)));
}

/// emitPrologue - Push callee-saved registers onto the stack, which
/// automatically adjust the stack pointer. Adjust the stack pointer to allocate
/// space for local variables.
void Z80FrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  int StackSize = -int(MFI.getStackSize());
  MCRegister ScratchReg = Is24Bit ? Z80::UHL : Z80::HL;

  // skip callee-saved saves
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
    ++MBBI;

  int FPOffset = -1;
  if (hasFP(MF)) {
    Register FrameReg = TRI->getFrameRegister(MF);
    if (MF.getFunction().hasOptSize()) {
      if (StackSize) {
        BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri),
                ScratchReg)
            .addImm(StackSize)
            .setMIFlag(MachineInstr::FrameSetup);
        BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::CALL24 : Z80::CALL16))
            .addExternalSymbol("_frameset")
            .addReg(ScratchReg, RegState::ImplicitKill)
            .addRegMask(TRI->getNoPreservedMask())
            .setMIFlag(MachineInstr::FrameSetup);
      } else
        BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::CALL24 : Z80::CALL16))
            .addExternalSymbol("_frameset0")
            .addRegMask(TRI->getNoPreservedMask())
            .setMIFlag(MachineInstr::FrameSetup);
      if (MF.needsFrameMoves()) {
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
                nullptr, TRI->getDwarfRegNum(FrameReg, true), 2 * SlotSize)));
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(MF.addFrameInst(MCCFIInstruction::createOffset(
                nullptr, TRI->getDwarfRegNum(FrameReg, true), -2 * SlotSize)));
      }
      return;
    }

    if (isFPSaved(MF)) {
      BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
          .addReg(FrameReg)
          .setMIFlag(MachineInstr::FrameSetup);
      if (MF.needsFrameMoves()) {
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(MF.addFrameInst(
                MCCFIInstruction::cfiDefCfaOffset(nullptr, 2 * SlotSize)));
        BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(MF.addFrameInst(MCCFIInstruction::createOffset(
                nullptr, TRI->getDwarfRegNum(FrameReg, true), -2 * SlotSize)));
      }
    }

    BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri),
            FrameReg)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::ADD24as : Z80::ADD16as),
            FrameReg)
        .addReg(FrameReg)
        .setMIFlag(MachineInstr::FrameSetup)
        ->addRegisterDead(Z80::F, TRI);
    FPOffset = 0;
    if (MF.needsFrameMoves())
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
              nullptr, TRI->getDwarfRegNum(FrameReg, true))));
  }

  BuildStackAdjustment(MF, MBB, MBBI, DL, ScratchReg, StackSize, FPOffset,
                       MachineInstr::FrameSetup);
}

void Z80FrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();

  DebugLoc DL = MBB.findDebugLoc(MBBI);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  int StackSize = int(MFI.getStackSize());

  const TargetRegisterClass *ScratchRC =
      Is24Bit ? &Z80::A24RegClass : &Z80::A16RegClass;
  TargetRegisterClass::iterator ScratchReg = ScratchRC->begin();
  for (; MBBI->readsRegister(TRI->getSubReg(*ScratchReg, Z80::sub_low), TRI);
       ++ScratchReg)
    assert(ScratchReg != ScratchRC->end() &&
           "Could not allocate a scratch register!");
  bool HasFP = hasFP(MF);
  assert((HasFP || *ScratchReg != TRI->getFrameRegister(MF)) &&
         "Cannot allocate csr as scratch register!");

  // skip callee-saved restores
  while (MBBI != MBB.begin())
    if (!(--MBBI)->getFlag(MachineInstr::FrameDestroy)) {
      ++MBBI;
      break;
    }

  // consume stack adjustment
  while (MBBI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MBBI);
    unsigned Opc = PI->getOpcode();
    if ((Opc == Z80::POP24r || Opc == Z80::POP16r) &&
        PI->getOperand(0).isDead()) {
      StackSize += SlotSize;
    } else if (Opc == Z80::LD24sa || Opc == Z80::LD16sa) {
      bool Is24Bit = Opc == Z80::LD24sa;
      unsigned Reg = PI->getOperand(0).getReg();
      if (PI == MBB.begin())
        break;
      MachineBasicBlock::iterator AI = std::prev(PI);
      Opc = AI->getOpcode();
      if (AI == MBB.begin() || Opc != (Is24Bit ? Z80::ADD24as : Z80::ADD16as) ||
          AI->getOperand(0).getReg() != Reg ||
          AI->getOperand(1).getReg() != Reg)
        break;
      MachineBasicBlock::iterator LI = std::prev(AI);
      Opc = LI->getOpcode();
      if (Opc != (Is24Bit ? Z80::LD24ri : Z80::LD16ri) ||
          LI->getOperand(0).getReg() != Reg)
        break;
      StackSize += LI->getOperand(1).getImm();
      LI->removeFromParent();
      AI->removeFromParent();
    } else
      break;
    PI->removeFromParent();
  }

  BuildStackAdjustment(MF, MBB, MBBI, DL, *ScratchReg, StackSize,
                       HasFP ? StackSize : -1, MachineInstr::FrameDestroy,
                       MFI.hasVarSizedObjects());

  if (isFPSaved(MF)) {
    Register FrameReg = TRI->getFrameRegister(MF);
    BuildMI(MBB, MBBI, DL, TII.get(Is24Bit ? Z80::POP24r : Z80::POP16r),
            FrameReg)
        .setMIFlags(MachineInstr::FrameDestroy);
    if (MF.needsFrameMoves()) {
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
              nullptr, TRI->getDwarfRegNum(TRI->getStackRegister(), true),
              SlotSize)));
      BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(MF.addFrameInst(MCCFIInstruction::createRestore(
              nullptr, TRI->getDwarfRegNum(FrameReg, true))));
    }
  }
}

// Only non-nested non-nmi interrupts can use shadow registers.
static bool shouldUseShadow(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return F.getFnAttribute("interrupt").getValueAsString() == "Generic";
}

static MachineInstr& setImplicitUsesUndef(MachineInstr &MI) {
  for (MachineOperand &MO : MI.implicit_operands())
    if (MO.isReg() && MO.isUse())
      MO.setIsUndef();
  return MI;
}

void Z80FrameLowering::shadowCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, const DebugLoc &DL,
    MachineInstr::MIFlag Flag, const std::vector<CalleeSavedInfo> &CSI) const {
  assert(shouldUseShadow(*MBB.getParent()) &&
         "Can't use shadow registers in this function.");
  bool SaveAF = false, SaveG = false;
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    if (Reg == Z80::AF)
      SaveAF = true;
    else if (Z80::G24RegClass.contains(Reg) || Z80::G16RegClass.contains(Reg))
      SaveG = true;
  }
  if (SaveG)
    setImplicitUsesUndef(
        *BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::EXX24 : Z80::EXX16))
             .setMIFlag(Flag));
  if (SaveAF)
    setImplicitUsesUndef(
        *BuildMI(MBB, MI, DL, TII.get(Z80::EXAF)).setMIFlag(Flag));
}

static Z80MachineFunctionInfo::AltFPMode
shouldUseAltFP(MachineFunction &MF, MCRegister AltFPReg,
               const TargetRegisterInfo *TRI) {
  if (MF.getFunction().hasOptSize() || MF.getFrameInfo().hasVarSizedObjects() ||
      MF.getTarget().Options.DisableFramePointerElim(MF))
    return Z80MachineFunctionInfo::AFPM_None;
  if (!MF.getRegInfo().isPhysRegUsed(Z80::UIY))
    return Z80MachineFunctionInfo::AFPM_Full;
  MachineBasicBlock::iterator LastFrameIdx;
  bool AltFPModified = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (AltFPModified) {
        for (const MachineOperand &MO : MI.operands())
          if (MO.isFI() || (MO.isRegMask() && MO.clobbersPhysReg(AltFPReg)))
            return Z80MachineFunctionInfo::AFPM_None;
      } else if (MI.modifiesRegister(AltFPReg, TRI))
        AltFPModified = true;
    }
    AltFPModified = true;
  }
  return Z80MachineFunctionInfo::AFPM_Partial;
}

bool Z80FrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  auto &FuncInfo = *MF.getInfo<Z80MachineFunctionInfo>();
  // ZZ80 without FP doesn't need the alt-FP optimization.
  if (STI.isZZ80() && !hasFP(MF))
    FuncInfo.setUsesAltFP(Z80MachineFunctionInfo::AFPM_None);
  else
    FuncInfo.setUsesAltFP(
        shouldUseAltFP(MF, Is24Bit ? Z80::UIY : Z80::IY, TRI));
  MF.getRegInfo().freezeReservedRegs(MF);

  bool UseShadow = shouldUseShadow(MF);
  unsigned CalleeSavedFrameSize = isFPSaved(MF) ? SlotSize : 0;
  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i - 1].getReg();

    // Non-index registers can be spilled to shadow registers.
    if (UseShadow && !Z80::I24RegClass.contains(Reg) &&
        !Z80::I16RegClass.contains(Reg))
      continue;

    CalleeSavedFrameSize += SlotSize;
  }
  FuncInfo.setCalleeSavedFrameSize(CalleeSavedFrameSize);

  return true;
}

bool Z80FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  bool UseShadow = shouldUseShadow(MF);
  DebugLoc DL = MBB.findDebugLoc(MI);
  if (UseShadow)
    shadowCalleeSavedRegisters(MBB, MI, DL, MachineInstr::FrameSetup, CSI);
  for (unsigned i = CSI.size(); i != 0; --i) {
    unsigned Reg = CSI[i - 1].getReg();

    // Non-index registers can be spilled to shadow registers.
    if (UseShadow && !Z80::I24RegClass.contains(Reg) &&
        !Z80::I16RegClass.contains(Reg))
      continue;

    bool isLiveIn = MRI.isLiveIn(Reg);
    if (!isLiveIn)
      MBB.addLiveIn(Reg);

    // Decide whether we can add a kill flag to the use.
    bool CanKill = !isLiveIn;
    // Check if any subregister is live-in
    if (CanKill) {
      for (MCRegAliasIterator AReg(Reg, TRI, false); AReg.isValid(); ++AReg) {
        if (MRI.isLiveIn(*AReg)) {
          CanKill = false;
          break;
        }
      }
    }

    // Do not set a kill flag on values that are also marked as live-in. This
    // happens with the @llvm.returnaddress intrinsic and with arguments
    // passed in callee saved registers.
    // Omitting the kill flags is conservatively correct even if the live-in
    // is not used after all.
    if (Reg == Z80::AF)
      BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF))
          .setMIFlag(MachineInstr::FrameSetup);
    else
      BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
          .addReg(Reg, getKillRegState(CanKill))
          .setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}
bool Z80FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  const MachineFunction &MF = *MBB.getParent();
  bool UseShadow = shouldUseShadow(MF);
  DebugLoc DL = MBB.findDebugLoc(MI);
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();

    // Non-index registers can be spilled to shadow registers.
    if (UseShadow && !Z80::I24RegClass.contains(Reg) &&
        !Z80::I16RegClass.contains(Reg))
      continue;

    if (Reg == Z80::AF)
      BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::POP24AF : Z80::POP16AF))
          .setMIFlag(MachineInstr::FrameDestroy);
    else
      BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::POP24r : Z80::POP16r), Reg)
          .setMIFlag(MachineInstr::FrameDestroy);
  }
  if (UseShadow)
    shadowCalleeSavedRegisters(MBB, MI, DL, MachineInstr::FrameDestroy, CSI);
  return true;
}

void Z80FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setMaxCallFrameSize(0); // call frames are not implemented yet
  bool NeedsScavSlot = MF.getInfo<Z80MachineFunctionInfo>()->getHasIllegalLEA()
                    || MFI.estimateStackSize(MF) > 0x80 - 2;
  // ZZ80 without FP always needs a scavenging slot for SP-relative access.
  if (!NeedsScavSlot && STI.isZZ80() && !hasFP(MF))
    NeedsScavSlot = true;
  if (NeedsScavSlot) {
    int64_t MinFixedObjOffset = -int64_t(SlotSize);
    for (int I = MFI.getObjectIndexBegin(); I < 0; ++I)
      MinFixedObjOffset = std::min(MinFixedObjOffset, MFI.getObjectOffset(I));
    int FI = MFI.CreateFixedSpillStackObject(
        SlotSize, MinFixedObjOffset - SlotSize * (1 + isFPSaved(MF)));
    RS->addScavengingFrameIndex(FI);
    // ZZ80 SP-relative: may need a second slot when multiple frame accesses
    // need scratch registers simultaneously in the same basic block.
    if (STI.isZZ80() && !hasFP(MF)) {
      int FI2 = MFI.CreateFixedSpillStackObject(
          SlotSize, MinFixedObjOffset - SlotSize * (2 + isFPSaved(MF)));
      RS->addScavengingFrameIndex(FI2);
    }
  }

  // Emit extra CFI_INSTRUCTION as necessary.
  if (!MF.needsFrameMoves() || hasFP(MF))
    return;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      TII.applySPAdjust(MI);
}

MachineBasicBlock::iterator Z80FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  if (!hasReservedCallFrame(MF)) {
    if (int Amount = TII.getFrameAdjustment(*I)) {
      Register ScratchReg = I->getOperand(I->getNumOperands() - 1).getReg();
      assert(ScratchReg.isPhysical() &&
             "Reg alloc should have already happened.");
      BuildStackAdjustment(MF, MBB, I, I->getDebugLoc(), ScratchReg, Amount);
    }
  }

  return MBB.erase(I);
}
