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
using namespace llvm;

#define DEBUG_TYPE "z80-frame-lowering"

Z80FrameLowering::Z80FrameLowering(const Z80Subtarget &STI)
    : TargetFrameLowering(StackGrowsDown, Align(1), STI.is24Bit() ? -3 : -2),
      STI(STI), TII(*STI.getInstrInfo()), TRI(STI.getRegisterInfo()),
      Is24Bit(STI.is24Bit()), SlotSize(Is24Bit ? 3 : 2) {
}

/// hasFP - Return true if the specified function should have a dedicated frame
/// pointer register.  This is true if the function has variable sized allocas
/// or if frame pointer elimination is disabled.
bool Z80FrameLowering::hasFP(const MachineFunction &MF) const {
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
    MF.getFrameInfo().hasStackObjects();
}

Z80FrameLowering::StackAdjustmentMethod
Z80FrameLowering::getOptimalStackAdjustmentMethod(MachineFunction &MF,
                                                  int Offset, int FPOffset,
                                                  bool ScratchIsIndex,
                                                  bool UnknownOffset) const {
  if (!Offset)
    return SAM_None;

  bool OptSize = MF.getFunction().getAttributes()
    .hasAttribute(AttributeList::FunctionIndex, Attribute::OptimizeForSize);
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

  if (UnknownOffset || (FPOffset >= 0 && FPOffset == Offset)) {
    // Optimal if we are trying to set SP = FP, except for tiny Offset
    unsigned AllCost = 0;
    //   LD SP, FP
    AllCost += OptSize || HasEZ80Ops ? 2 : 10;

    return AllCost <= BestCost ? SAM_All : BestMethod;
  }

  if (HasEZ80Ops && FPOffset >= 0 && isInt<8>(Offset - FPOffset) && hasFP(MF)) {
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

  // Optimal for large offsets
  unsigned LargeCost = 0;
  //   LD <scratch>, Offset
  LargeCost += OptSize || HasEZ80Ops ? 1 + SlotSize : 10;
  //   ADD <scratch>, SP
  LargeCost += OptSize || HasEZ80Ops ? 1 : 11;
  //   LD SP, <scratch>
  LargeCost += OptSize || HasEZ80Ops ? 1 : 6;
  if (ScratchIsIndex)
    LargeCost += OptSize || HasEZ80Ops ? 3 : 12;

  return LargeCost < BestCost ? SAM_Large : BestMethod;
}

void Z80FrameLowering::BuildStackAdjustment(MachineFunction &MF,
                                            MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MI,
                                            DebugLoc DL, Register ScratchReg,
                                            int Offset, int FPOffset,
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
      BuildMI(MBB, MI, DL,
              TII.get(Offset >= 0 ? (Is24Bit ? Z80::POP24r : Z80::POP16r)
                                  : (Is24Bit ? Z80::PUSH24r : Z80::PUSH16r)))
          .addReg(ScratchReg, getDefRegState(Offset >= 0) |
                                  getDeadRegState(Offset >= 0) |
                                  getUndefRegState(Offset < 0));
    LLVM_FALLTHROUGH;
  case SAM_Tiny:
    for (unsigned IncDecCount = std::abs(Offset) % SlotSize; IncDecCount;
         --IncDecCount)
      BuildMI(MBB, MI, DL,
              TII.get(Offset >= 0 ? (Is24Bit ? Z80::INC24SP : Z80::INC16SP)
                                  : (Is24Bit ? Z80::DEC24SP : Z80::DEC16SP)));
    return;
  case SAM_All:
    // Just store FP to SP
    ResultReg = FrameReg;
    break;
  case SAM_Medium:
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::LEA24ro : Z80::LEA16ro),
            ScratchReg).addUse(FrameReg).addImm(Offset - FPOffset);
    ResultReg = ScratchReg;
    break;
  case SAM_Large:
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri),
            ScratchReg).addImm(Offset);
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::ADD24SP : Z80::ADD16SP),
            ScratchReg).addUse(ScratchReg);
    ResultReg = ScratchReg;
    break;
  }
  BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::LD24SP : Z80::LD16SP))
      .addUse(ResultReg, RegState::Kill);
}

/// emitPrologue - Push callee-saved registers onto the stack, which
/// automatically adjust the stack pointer. Adjust the stack pointer to allocate
/// space for local variables.
void Z80FrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MI = MBB.begin();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  int StackSize = -int(MFI.getStackSize());
  unsigned ScratchReg = Is24Bit ? Z80::UHL : Z80::HL;

  // skip callee-saved saves
  while (MI != MBB.end() && MI->getFlag(MachineInstr::FrameSetup))
    ++MI;

  int FPOffset = -1;
  if (hasFP(MF)) {
    if (MF.getFunction().getAttributes().hasAttribute(
            AttributeList::FunctionIndex, Attribute::OptimizeForSize)) {
      if (StackSize) {
        BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri),
                ScratchReg).addImm(StackSize);
        BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::CALL24 : Z80::CALL16))
            .addExternalSymbol("_frameset")
            .addUse(ScratchReg, RegState::ImplicitKill);
        return;
      }
      BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::CALL24 : Z80::CALL16))
        .addExternalSymbol("_frameset0");
      return;
    }
    unsigned FrameReg = TRI->getFrameRegister(MF);
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
        .addUse(FrameReg);
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::LD24ri : Z80::LD16ri), FrameReg)
        .addImm(0);
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::ADD24SP : Z80::ADD16SP),
            FrameReg).addUse(FrameReg);
    FPOffset = 0;
  }
  BuildStackAdjustment(MF, MBB, MI, DL, ScratchReg, StackSize, FPOffset);
}

void Z80FrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MI = MBB.getFirstTerminator();
  DebugLoc DL = MBB.findDebugLoc(MI);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  int StackSize = int(MFI.getStackSize());

  const TargetRegisterClass *ScratchRC = Is24Bit ? &Z80::A24RegClass
                                                 : &Z80::A16RegClass;
  TargetRegisterClass::iterator ScratchReg = ScratchRC->begin();
  for (; MI->readsRegister(TRI->getSubReg(*ScratchReg, Z80::sub_low), TRI);
       ++ScratchReg)
    assert(ScratchReg != ScratchRC->end() &&
           "Could not allocate a scratch register!");
  assert((hasFP(MF) || *ScratchReg != TRI->getFrameRegister(MF)) &&
         "Cannot allocate csr as scratch register!");

  // skip callee-saved restores
  while (MI != MBB.begin())
    if (!(--MI)->getFlag(MachineInstr::FrameDestroy)) {
      ++MI;
      break;
    }

  // consume stack adjustment
  while (MI != MBB.begin()) {
    MachineBasicBlock::iterator PI = std::prev(MI);
    unsigned Opc = PI->getOpcode();
    if ((Opc == Z80::POP24r || Opc == Z80::POP16r) &&
        PI->getOperand(0).isDead()) {
      StackSize += SlotSize;
    } else if (Opc == Z80::LD24SP || Opc == Z80::LD16SP) {
      bool Is24Bit = Opc == Z80::LD24SP;
      unsigned Reg = PI->getOperand(0).getReg();
      if (PI == MBB.begin())
        break;
      MachineBasicBlock::iterator AI = std::prev(PI);
      Opc = AI->getOpcode();
      if (AI == MBB.begin() || Opc != (Is24Bit ? Z80::ADD24SP : Z80::ADD16SP) ||
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

  bool HasFP = hasFP(MF);
  BuildStackAdjustment(MF, MBB, MI, DL, *ScratchReg, StackSize,
                       HasFP ? StackSize : -1, MFI.hasVarSizedObjects());
  if (HasFP)
    BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::POP24r : Z80::POP16r),
            TRI->getFrameRegister(MF));
}

// Only non-nested non-nmi interrupts can use shadow registers.
static bool shouldUseShadow(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return F.getFnAttribute("interrupt").getValueAsString() == "Generic";
}

void Z80FrameLowering::shadowCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, DebugLoc DL,
    MachineInstr::MIFlag Flag, const std::vector<CalleeSavedInfo> &CSI) const {
  assert(shouldUseShadow(*MBB.getParent()) &&
         "Can't use shadow registers in this function.");
  bool SaveAF = false, SaveG = false;
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();
    if (Reg == Z80::AF)
      SaveAF = true;
    else if (Z80::G24RegClass.contains(Reg) ||
             Z80::G16RegClass.contains(Reg))
      SaveG = true;
  }
  if (SaveAF)
    BuildMI(MBB, MI, DL, TII.get(Z80::EXAF))
      .setMIFlag(Flag);
  if (SaveG)
    BuildMI(MBB, MI, DL, TII.get(Z80::EXX))
      .setMIFlag(Flag);
}

bool Z80FrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  MF.getInfo<Z80MachineFunctionInfo>()
    ->setCalleeSavedFrameSize((CSI.size() + hasFP(MF)) * SlotSize);
  return true;
}

bool Z80FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    const std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
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
    // happens with the @llvm-returnaddress intrinsic and with arguments
    // passed in callee saved registers.
    // Omitting the kill flags is conservatively correct even if the live-in
    // is not used after all.
    MachineInstrBuilder MIB;
    if (Reg == Z80::AF)
      MIB = BuildMI(MBB, MI, DL,
                    TII.get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF));
    else
      MIB = BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
                .addUse(Reg, getKillRegState(CanKill));
    MIB.setMIFlag(MachineInstr::FrameSetup);
  }
  return true;
}
bool Z80FrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    std::vector<CalleeSavedInfo> &CSI,
    const TargetRegisterInfo *TRI) const {
  const MachineFunction &MF = *MBB.getParent();
  bool UseShadow = shouldUseShadow(MF);
  DebugLoc DL = MBB.findDebugLoc(MI);
  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    unsigned Reg = CSI[i].getReg();

    // Non-index registers can be spilled to shadow registers.
    if (UseShadow && !Z80::I24RegClass.contains(Reg) &&
                     !Z80::I16RegClass.contains(Reg))
      continue;

    MachineInstrBuilder MIB;
    if (Reg == Z80::AF)
      MIB = BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::POP24AF
                                                 : Z80::POP16AF));
    else
      MIB = BuildMI(MBB, MI, DL, TII.get(Is24Bit ? Z80::POP24r : Z80::POP16r),
                    Reg);
    MIB.setMIFlag(MachineInstr::FrameDestroy);
  }
  if (UseShadow)
    shadowCalleeSavedRegisters(MBB, MI, DL, MachineInstr::FrameDestroy, CSI);
  return true;
}

void Z80FrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  Z80MachineFunctionInfo &FuncInfo = *MF.getInfo<Z80MachineFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setMaxCallFrameSize(0); // call frames are not implemented atm
  if (FuncInfo.getHasIllegalLEA() || MFI.estimateStackSize(MF) > 0x80) {
    int64_t MinFixedObjOffset = -int64_t(SlotSize);
    for (int I = MFI.getObjectIndexBegin(); I < 0; ++I)
      MinFixedObjOffset = std::min(MinFixedObjOffset, MFI.getObjectOffset(I));
    int FI = MFI.CreateFixedSpillStackObject(
        SlotSize, MinFixedObjOffset - SlotSize * (1 + hasFP(MF)));
    RS->addScavengingFrameIndex(FI);
  }
}

MachineBasicBlock::iterator Z80FrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  if (!hasReservedCallFrame(MF)) {
    if (int Amount = TII.getFrameAdjustment(*I)) {
      Register ScratchReg = I->getOperand(I->getNumOperands() - 1).getReg();
      assert(Register::isPhysicalRegister(ScratchReg) &&
             "Reg alloc should have already happened.");
      BuildStackAdjustment(MF, MBB, I, I->getDebugLoc(), ScratchReg, Amount);
    }
  }

  return MBB.erase(I);
}
