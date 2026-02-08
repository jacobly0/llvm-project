//=== lib/Target/Z80/Z80MachineLateOptimization.cpp -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// after register allocation.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80RegisterInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"

#define DEBUG_TYPE "z80-machine-late-opt"

using namespace llvm;

namespace {
class RegVal {
  static constexpr int UnknownOff = ~0;
  const GlobalValue *GV = nullptr;
  int Off = UnknownOff, Mask = 0;
  MachineInstr *DeadOrKilledBy = nullptr;

public:
  RegVal() {}
  RegVal(MCRegister Reg, const TargetRegisterInfo &TRI) {
    if (auto *RC = TRI.getMinimalPhysRegClass(Reg))
      Mask = maskTrailingOnes<unsigned>(TRI.getRegSizeInBits(*RC));
  }
  RegVal(const MachineOperand &MO, MCRegister Reg,
         const TargetRegisterInfo &TRI)
      : RegVal(Reg, TRI) {
    switch (MO.getType()) {
    case MachineOperand::MO_Immediate:
      Off = MO.getImm();
      break;
    case MachineOperand::MO_CImmediate:
      Off = MO.getCImm()->getZExtValue();
      break;
    case MachineOperand::MO_GlobalAddress:
      GV = MO.getGlobal();
      Off = MO.getOffset();
      break;
    default:
      return;
    }
    Off &= Mask;
    assert(valid() && "Mask should have been less than 32 bits");
  }
  RegVal(int Imm, MCRegister Reg, const TargetRegisterInfo &TRI)
      : RegVal(Reg, TRI) {
    Off = Imm & Mask;
    assert(valid() && "Mask should have been less than 32 bits");
  }
  RegVal(int Imm, int KnownMask, MCRegister Reg, const TargetRegisterInfo &TRI)
      : RegVal{Imm, Reg, TRI} {
    Mask &= KnownMask;
  }
  RegVal(const RegVal &SuperVal, unsigned Idx, MCRegister Reg,
         const TargetRegisterInfo &TRI) {
    Mask = maskTrailingOnes<unsigned>(TRI.getSubRegIdxSize(Idx));
    if (!SuperVal.isImm())
      return;
    Off = SuperVal.Off >> TRI.getSubRegIdxOffset(Idx) & Mask;
    assert(valid() && "Mask should have been less than 32 bits");
  }

  void clobber() {
    Off = UnknownOff;
    DeadOrKilledBy = nullptr;
  }

  bool valid() const { return Off != UnknownOff; }
  bool isImm() const { return valid() && !GV; }
  bool isGlobal() const { return valid() && GV; }

  bool matches(const RegVal &Val, int Delta = 0) const {
    return valid() && Val.valid() && GV == Val.GV && Mask == Val.Mask &&
           Off == ((Val.Off + Delta) & Mask);
  }
  std::pair<int, int> getKnownBits(int KnownMask = ~0) const {
    if (!isImm())
      return {0, 0};
    return {Off & KnownMask, Mask & KnownMask};
  }

  void setDeadOrKilledBy(MachineInstr *MI) { DeadOrKilledBy = MI; }
  MachineInstr *takeDeadOrKilledBy() {
    return std::exchange(DeadOrKilledBy, nullptr);
  }

#ifndef NDEBUG
  friend raw_ostream &operator<<(raw_ostream &OS, RegVal &Val) {
    unsigned Width = 2 + divideCeil(1 + Log2_32(Val.Mask), 4);
    OS << " & " << format_hex(Val.Mask, Width, true) << " == ";
    if (!Val.valid())
      return OS << "?";
    if (Val.GV)
      OS << Val.GV << '+';
    return OS << format_hex(Val.Off, Width, true);
  }
#endif
};

class Z80MachineLateOptimization : public MachineFunctionPass {
  enum {
    CarryFlag = 1 << 0,
    SubtractFlag = 1 << 1,
    ParityOverflowFlag = 1 << 2,
    HalfCarryFlag = 1 << 4,
    ZeroFlag = 1 << 6,
    SignFlag = 1 << 7,
  };

  const TargetRegisterInfo *TRI;
  RegVal RegVals[Z80::NUM_TARGET_REGS];

  template <typename... Args> void assign(MCRegister Reg, Args &&...args) {
    RegVals[Reg] = RegVal(std::forward<Args>(args)..., Reg, *TRI);
  }
  void clobberAll() {
    for (unsigned Reg = 1; Reg != Z80::NUM_TARGET_REGS; ++Reg)
      assign(Reg);
  }
  template <typename MCRegIterator>
  void clobber(MCRegister Reg, bool IncludeSelf) {
    for (MCRegIterator I(Reg, TRI, IncludeSelf); I.isValid(); ++I)
      assign(*I);
  }
  void updateDeadOrKilledBy(MachineInstr *MI,
                            bool (MachineOperand::*Pred)() const) {
    for (const MachineOperand &MO : MI->operands())
      if (MO.isReg() && (MO.*Pred)())
        for (MCRegAliasIterator I(MO.getReg(), TRI, true); I.isValid(); ++I)
          RegVals[*I].setDeadOrKilledBy(MI);
  }
  bool reuse(MCRegister Reg) {
    if (MachineInstr *DeadOrKilledBy = RegVals[Reg].takeDeadOrKilledBy()) {
      if (DeadOrKilledBy->registerDefIsDead(Reg, TRI)) {
        DeadOrKilledBy->clearRegisterDeads(Reg);
        return true;
      }
      if (DeadOrKilledBy->killsRegister(Reg, TRI)) {
        DeadOrKilledBy->clearRegisterKills(Reg, TRI);
        return true;
      }
    }
    return false;
  }

  bool isKnownSpecificImm(MCRegister Reg, int Val) const {
    return RegVals[Reg].matches({Val, Reg, *TRI});
  }
  bool isKnownSpecificImm(const MachineOperand &MO, int Val) const {
    if (MO.isImm())
      return MO.getImm() == Val;
    if (MO.isCImm())
      return MO.getCImm()->getSExtValue() == Val;
    return MO.isReg() && isKnownSpecificImm(MO.getReg(), Val);
  }

  void debug(const MachineInstr &MI);

  std::tuple<uint8_t, uint8_t, MCRegister, RegVal>
  getKnownVal(const MachineInstr &MI) const;

  struct KnownFlags {
    static const KnownFlags PreserveDocumented;
    const uint8_t SetMask = 0, ResetMask = 0, PreserveMask = 0;
    operator bool() const { return SetMask | ResetMask; }
    uint8_t getKnownVal(uint8_t KnownFlagsVal) const {
      return SetMask | (KnownFlagsVal & PreserveMask);
    }
    uint8_t getKnownMask(uint8_t KnownFlagsMask) const {
      return SetMask | ResetMask | (KnownFlagsMask & PreserveMask);
    }
  };
  KnownFlags getKnownFlags(const MachineInstr &MI, uint8_t KnownFlagsVal,
                           uint8_t KnownFlagsMask) const;

public:
  static char ID;

  Z80MachineLateOptimization() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Z80 Machine Late Optimization";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

Z80MachineLateOptimization::KnownFlags const
    Z80MachineLateOptimization::KnownFlags::PreserveDocumented{
        0, 0,
        SignFlag | ZeroFlag | HalfCarryFlag | ParityOverflowFlag |
            SubtractFlag | CarryFlag};

void Z80MachineLateOptimization::debug(const MachineInstr &MI) {
  for (unsigned Reg = 1; Reg != Z80::NUM_TARGET_REGS; ++Reg)
    if (RegVals[Reg].valid())
      LLVM_DEBUG(dbgs() << TRI->getName(Reg) << RegVals[Reg] << ", ");
  LLVM_DEBUG(MI.dump());
}

std::tuple<uint8_t, uint8_t, MCRegister, RegVal>
Z80MachineLateOptimization::getKnownVal(const MachineInstr &MI) const {
  uint8_t KnownFlagsVal, KnownFlagsMask;
  std::tie(KnownFlagsVal, KnownFlagsMask) = RegVals[Z80::F].getKnownBits();
  MCRegister DstReg;
  RegVal DstVal;
  if (MI.getNumExplicitDefs() == 1)
    DstReg = MI.getOperand(0).getReg();
  switch (unsigned Opc = MI.getOpcode()) {
  case Z80::RCF: case Z80::SCF:
    DstReg = Z80::F;
    if (!(KnownFlagsMask & CarryFlag) ||
        (Opc == Z80::RCF) ^ !(KnownFlagsVal & CarryFlag))
      break;
    DstVal = {KnownFlagsVal, KnownFlagsMask, DstReg, *TRI};
    break;
  case Z80:: LD8ai: case Z80::LD16ai: case Z80::LD24ai:
  case Z80:: LD8ar: case Z80::LD8amb: case Z80:: LD8am: case Z80:: LD8ap:
  case Z80::SExt8: case Z80::CPL: case Z80::NEG: case Z80::DAA:
  case Z80::RLCA: case Z80::RRCA: case Z80::RLA: case Z80::RRA:
  case Z80::RRD16: case Z80::RLD16: case Z80::RRD24: case Z80::RLD24:
  case Z80::ADD8ar: case Z80::ADD8ai: case Z80::ADD8ap: case Z80::ADD8ao:
  case Z80::ADC8ar: case Z80::ADC8ai: case Z80::ADC8ap: case Z80::ADC8ao:
  case Z80::SUB8ar: case Z80::SUB8ai: case Z80::SUB8ap: case Z80::SUB8ao:
  case Z80::SBC8ar: case Z80::SBC8ai: case Z80::SBC8ap: case Z80::SBC8ao:
  case Z80::AND8ar: case Z80::AND8ai: case Z80::AND8ap: case Z80::AND8ao:
  case Z80::XOR8ar: case Z80::XOR8ai: case Z80::XOR8ap: case Z80::XOR8ao:
  case Z80:: OR8ar: case Z80:: OR8ai: case Z80:: OR8ap: case Z80:: OR8ao:
  case Z80:: CP8ar: case Z80:: CP8ai: case Z80:: CP8ap: case Z80:: CP8ao:
  case Z80::TST8ag: case Z80::TST8ai: case Z80::TST8ap:
    DstReg = Z80::A;
    switch (Opc) {
    case Z80::SUB8ar:
    case Z80::XOR8ar:
      if (MI.getOperand(0).getReg() != DstReg)
        break;
      DstVal = {0, DstReg, *TRI};
      break;
    }
    break;
  case Z80:: LD8ia: case Z80::LD16ia:
    DstReg = Z80::I;
    break;
  case Z80::LD8ra:
    DstReg = Z80::R;
    break;
  case Z80::LD8mba:
    DstReg = Z80::MB;
    break;
  case Z80::POP16AF: case Z80::POP24AF:
    DstReg = Z80::AF;
    break;
  case Z80::INC16s: case Z80::DEC16s:
    DstReg = Z80::SPS;
    break;
  case Z80::INC24s: case Z80::DEC24s:
    DstReg = Z80::SPL;
    break;
  case Z80::LD8ri:
  case Z80::LD16ri:
  case Z80::LD24ri:
    DstVal = {MI.getOperand(1), DstReg, *TRI};
    break;
  case Z80::LD8r0:
    DstVal = {0, DstReg, *TRI};
    break;
  case Z80::LD24r0:
    DstVal = {0, DstReg, *TRI};
    break;
  case Z80::LD24r_1:
    DstVal = {-1, DstReg, *TRI};
    break;
  case Z80::SExt16: case Z80::Sub16ao:
  case Z80::SBC16aa: case Z80::SBC16ao: case Z80::SBC16as:
  case Z80::ADC16aa: case Z80::ADC16ao: case Z80::ADC16as:
    DstReg = Z80::HL;
    break;
  case Z80::SExt24: case Z80::Sub24ao:
  case Z80::SBC24aa: case Z80::SBC24ao: case Z80::SBC24as:
  case Z80::ADC24aa: case Z80::ADC24ao: case Z80::ADC24as:
    DstReg = Z80::UHL;
    break;
  }
  return {KnownFlagsVal, KnownFlagsMask, DstReg, DstVal};
}

Z80MachineLateOptimization::KnownFlags
Z80MachineLateOptimization::getKnownFlags(const MachineInstr &MI,
                                          uint8_t KnownFlagsVal,
                                          uint8_t KnownFlagsMask) const {
  switch (unsigned Opc = MI.getOpcode()) {
  case Z80::LD8r0:
    if (MI.getOperand(0).getReg() != Z80::A)
      return KnownFlags::PreserveDocumented;
    return {ZeroFlag, SignFlag | HalfCarryFlag | ParityOverflowFlag |
                          SubtractFlag | CarryFlag};
  case Z80::LD24r0:
    if (MI.getOperand(0).getReg() != Z80::UHL)
      return KnownFlags::PreserveDocumented;
    return {ZeroFlag | SubtractFlag,
            SignFlag | HalfCarryFlag | ParityOverflowFlag | CarryFlag};
  case Z80::LD24r_1:
    if (MI.getOperand(0).getReg() != Z80::UHL)
      return KnownFlags::PreserveDocumented;
    return {SignFlag | HalfCarryFlag | SubtractFlag | CarryFlag,
            ZeroFlag | ParityOverflowFlag};
  case Z80::SCF:
    return {CarryFlag, HalfCarryFlag | SubtractFlag,
            SignFlag | ZeroFlag | ParityOverflowFlag};
  case Z80::CCF:
    return {0, SubtractFlag, SignFlag | ZeroFlag | ParityOverflowFlag};
  case Z80::ADD8ar:
  case Z80::ADD8ai:
    if (isKnownSpecificImm(Z80::A, 0) ||
        isKnownSpecificImm(MI.getOperand(0), 0))
      return {0, HalfCarryFlag | ParityOverflowFlag | SubtractFlag | CarryFlag};
    LLVM_FALLTHROUGH;
  case Z80::ADD8ap:
  case Z80::ADD8ao:
  case Z80::ADC8ar:
  case Z80::ADC8ai:
  case Z80::ADC8ap:
  case Z80::ADC8ao:
    return {0, SubtractFlag};
  case Z80::SUB8ar:
  case Z80::SUB8ai:
    if (isKnownSpecificImm(MI.getOperand(0), 0))
      return {SubtractFlag, HalfCarryFlag | ParityOverflowFlag | CarryFlag};
    LLVM_FALLTHROUGH;
  case Z80::SUB8ap:
  case Z80::SUB8ao:
  case Z80::SBC8ar:
  case Z80::SBC8ai:
  case Z80::SBC8ap:
  case Z80::SBC8ao:
    if (Opc != Z80::SBC8ar || MI.getOperand(0).getReg() != Z80::A)
      return {SubtractFlag};
    if (!(KnownFlagsMask & CarryFlag))
      return {SubtractFlag, ParityOverflowFlag};
    if (KnownFlagsVal & CarryFlag)
      return {SignFlag | HalfCarryFlag | SubtractFlag | CarryFlag,
              ZeroFlag | ParityOverflowFlag};
    return {ZeroFlag | SubtractFlag,
            SignFlag | HalfCarryFlag | ParityOverflowFlag | CarryFlag};
  case Z80::AND8ar:
  case Z80::AND8ai:
  case Z80::AND8ap:
  case Z80::AND8ao:
    return {HalfCarryFlag, SubtractFlag, CarryFlag};
  case Z80::RCF:
  case Z80::XOR8ar:
  case Z80::XOR8ai:
  case Z80::XOR8ap:
  case Z80::XOR8ao:
  case Z80::OR8ar:
  case Z80::OR8ai:
  case Z80::OR8ap:
  case Z80::OR8ao:
    return {0, HalfCarryFlag | SubtractFlag, CarryFlag};
  case Z80::ADD16as:
  case Z80::ADD24as:
    if (isKnownSpecificImm(Opc == Z80::ADD16as ? Z80::SPS : Z80::SPL, 0))
      return {0, HalfCarryFlag | SubtractFlag | CarryFlag, SignFlag | ZeroFlag};
    LLVM_FALLTHROUGH;
  case Z80::ADD16aa:
  case Z80::ADD24aa:
  case Z80::ADD16ao:
  case Z80::ADD24ao:
    for (const MachineOperand &MO : MI.explicit_uses())
      if (isKnownSpecificImm(MO, 0))
        return {0, HalfCarryFlag | SubtractFlag | CarryFlag,
                SignFlag | ZeroFlag};
    return {0, SubtractFlag, SignFlag | ZeroFlag};
  case Z80::ADC16aa:
  case Z80::ADC24aa:
    return {0, SubtractFlag};
  case Z80::ADC16ao:
  case Z80::ADC24ao:
  case Z80::ADC16as:
  case Z80::ADC24as:
    return {0, SubtractFlag};
  case Z80::SBC16aa:
  case Z80::SBC24aa:
    if (!(KnownFlagsMask & CarryFlag))
      return {SubtractFlag, ParityOverflowFlag};
    if (KnownFlagsVal & CarryFlag)
      return {SignFlag | HalfCarryFlag | SubtractFlag | CarryFlag,
              ZeroFlag | ParityOverflowFlag};
    return {ZeroFlag | SubtractFlag,
            SignFlag | HalfCarryFlag | ParityOverflowFlag | CarryFlag};
  case Z80::SBC16ao:
  case Z80::SBC24ao:
    if ((~KnownFlagsVal & KnownFlagsMask & CarryFlag) &&
        (isKnownSpecificImm(Opc == Z80::SBC16ao ? Z80::HL : Z80::UHL, 0) ||
         isKnownSpecificImm(MI.getOperand(0), 0)))
      return {SubtractFlag, HalfCarryFlag | ParityOverflowFlag | CarryFlag};
    return {SubtractFlag};
  case Z80::SBC16as:
  case Z80::SBC24as:
    if ((~KnownFlagsVal & KnownFlagsMask & CarryFlag) &&
        (isKnownSpecificImm(Opc == Z80::SBC16as ? Z80::HL : Z80::UHL, 0) ||
         isKnownSpecificImm(Opc == Z80::SBC16as ? Z80::SPS : Z80::SPL, 0)))
      return {SubtractFlag, HalfCarryFlag | ParityOverflowFlag | CarryFlag};
    return {SubtractFlag};
  case Z80::Sub16ao:
  case Z80::Sub24ao:
    return {SubtractFlag};
  }
  return {};
}

bool Z80MachineLateOptimization::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  TRI = MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  LiveRegUnits LiveUnits(*TRI);
  for (MachineBasicBlock &MBB : MF) {
    LiveUnits.clear();
    LiveUnits.addLiveIns(MBB);
    clobberAll();
    for (MachineBasicBlock::iterator I = MBB.begin(), E = MBB.end(); I != E;) {
      MachineInstrBuilder MIB(MF, I);
      LiveUnits.stepForward(*I);
      ++I;

      uint8_t KnownFlagsVal, KnownFlagsMask;
      MCRegister DstReg;
      RegVal DstVal;
      std::tie(KnownFlagsVal, KnownFlagsMask, DstReg, DstVal) =
          getKnownVal(*MIB);

      switch (unsigned Opc = MIB->getOpcode()) {
      case Z80::LD8r0:
      case Z80::LD8ri:
      case Z80::LD8pi:
      case Z80::LD8oi:
      case Z80::ADD8ai:
      case Z80::ADC8ai:
      case Z80::SUB8ai:
      case Z80::SBC8ai:
      case Z80::AND8ai:
      case Z80::XOR8ai:
      case Z80::OR8ai:
      case Z80::CP8ai:
      case Z80::TST8ai: {
        MachineOperand *ImmMO;
        RegVal ImmVal;
        if (Opc == Z80::LD8r0) {
          if (DstReg == Z80::A)
            break;
          ImmMO = nullptr;
          ImmVal = {0, DstReg, *TRI};
        } else {
          ImmMO = &MIB->getOperand(MIB->getNumExplicitOperands() - 1);
          ImmVal = {*ImmMO, Z80::A, *TRI};
        }
        for (MCRegister SrcReg = Z80::NoRegister + 1;
             SrcReg != Z80::NUM_TARGET_REGS; SrcReg = SrcReg + 1) {
          if (!RegVals[SrcReg].matches(ImmVal))
            continue;
          unsigned NewOpc = Z80::INSTRUCTION_LIST_END;
          switch (Opc) {
          default:
            llvm_unreachable("Unexpected opcode");
          case Z80::LD8r0:
          case Z80::LD8ri:
            if (Z80::G8RegClass.contains(DstReg) &&
                Z80::G8RegClass.contains(SrcReg))
              NewOpc = Z80::LD8gg;
            else if (Z80::X8RegClass.contains(DstReg) &&
                     Z80::X8RegClass.contains(SrcReg))
              NewOpc = Z80::LD8xx;
            else if (Z80::Y8RegClass.contains(DstReg) &&
                     Z80::Y8RegClass.contains(SrcReg))
              NewOpc = Z80::LD8yy;
            break;
          case Z80::LD8pi:
            if (Z80::G8RegClass.contains(SrcReg))
              NewOpc = Z80::LD8pg;
            break;
          case Z80::LD8oi:
            if (Z80::G8RegClass.contains(SrcReg))
              NewOpc = Z80::LD8og;
            break;
          case Z80::ADD8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::ADD8ar;
            break;
          case Z80::ADC8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::ADC8ar;
            break;
          case Z80::SUB8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::SUB8ar;
            break;
          case Z80::SBC8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::SBC8ar;
            break;
          case Z80::AND8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::AND8ar;
            break;
          case Z80::XOR8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80::XOR8ar;
            break;
          case Z80:: OR8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80:: OR8ar;
            break;
          case Z80:: CP8ai:
            if (Z80::R8RegClass.contains(SrcReg))
              NewOpc = Z80:: CP8ar;
            break;
          case Z80::TST8ai:
            if (Z80::G8RegClass.contains(SrcReg))
              NewOpc = Z80::TST8ag;
            break;
          }
          if (NewOpc == Z80::INSTRUCTION_LIST_END)
            continue;
          MIB->setDesc(TII.get(NewOpc));
          if (ImmMO) {
            ImmMO->ChangeToRegister(SrcReg, /*isDef=*/false, /*isImp=*/false,
                                    /*isKill=*/reuse(SrcReg));
            break;
          }
          assert(Opc == Z80::LD8r0);
          MIB.addReg(SrcReg, getKillRegState(reuse(SrcReg)));
          int ImpDefIdx = MIB->findRegisterDefOperandIdx(Z80::F, true);
          if (ImpDefIdx >= 0)
            MIB->removeOperand(ImpDefIdx);
          break;
        }
        break;
      }
      case Z80::LD24r0:
      case Z80::LD24r_1:
        if (DstReg != Z80::UHL || !(KnownFlagsMask & CarryFlag) ||
            ((Opc == Z80::LD24r0) ^ !(KnownFlagsVal & CarryFlag)))
          break;
        LLVM_DEBUG(dbgs() << "Replacing: "; MIB->dump();
                   dbgs() << "     With: ");
        MIB->setDesc(TII.get(Z80::SBC24aa));
        MIB->getOperand(0).setImplicit();
        MIB->getOperand(1).setImplicit();
        MIB.addReg(DstReg, RegState::Implicit | RegState::Undef);
        MIB.addReg(Z80::F, RegState::Implicit | getKillRegState(reuse(Z80::F)));
        break;
      case Z80::SLA8g:
        if (DstReg != Z80::A || !LiveUnits.available(Z80::F))
          break;
        LLVM_DEBUG(dbgs() << "Replacing: "; MIB->dump();
                   dbgs() << "     With: ");
        MIB->setDesc(TII.get(Z80::ADD8ar));
        MIB->untieRegOperand(1);
        MIB->getOperand(0).setImplicit();
        MIB->getOperand(1).setImplicit();
        MIB.addReg(DstReg, getKillRegState(MIB->getOperand(1).isKill()));
        break;
      case Z80::RLC8g:
      case Z80::RRC8g:
      case Z80:: RL8g:
      case Z80:: RR8g:
        if (DstReg != Z80::A || !LiveUnits.available(Z80::F))
          break;
        switch (Opc) {
        case Z80::RLC8g: Opc = Z80::RLCA; break;
        case Z80::RRC8g: Opc = Z80::RRCA; break;
        case Z80:: RL8g: Opc = Z80:: RLA; break;
        case Z80:: RR8g: Opc = Z80:: RRA; break;
        }
        LLVM_DEBUG(dbgs() << "Replacing: "; MIB->dump();
                   dbgs() << "     With: ");
        MIB->setDesc(TII.get(Opc));
        MIB->getOperand(0).setImplicit();
        MIB->getOperand(1).setImplicit();
        break;
      case Z80::Sub16ao:
      case Z80::Sub24ao:
      case Z80::Cmp16ao:
      case Z80::Cmp24ao: {
        if (!(~KnownFlagsVal & KnownFlagsMask & CarryFlag))
          break;
        unsigned SubOpc, AddOpc;
        switch (Opc) {
        case Z80::Sub16ao:
        case Z80::Cmp16ao:
          SubOpc = Z80::SBC16ao;
          AddOpc = Z80::ADD16ao;
          break;
        case Z80::Sub24ao:
        case Z80::Cmp24ao:
          SubOpc = Z80::SBC24ao;
          AddOpc = Z80::ADD24ao;
          break;
        }
        LLVM_DEBUG(dbgs() << "Replacing: "; MIB->dump();
                   dbgs() << "     With: ");
        MIB->setDesc(TII.get(SubOpc));
        MIB.addReg(Z80::F, RegState::Implicit | getKillRegState(reuse(Z80::F)));
        switch (Opc) {
        case Z80::Cmp16ao:
        case Z80::Cmp24ao:
          MIB.addReg(DstReg, RegState::ImplicitDefine);
          if (LiveUnits.available(DstReg))
            break;
          MachineOperand &SrcMO = MIB->getOperand(0);
          MIB = BuildMI(MBB, I, MIB->getDebugLoc(), TII.get(AddOpc), DstReg)
                    .addReg(DstReg).add(SrcMO);
          SrcMO.setIsKill(false);
          break;
        }
        break;
      }
      case Z80::ADD16as: {
        // Peephole: LD16ri HL, imm / ADD16as HL, SP → LDA16hs_sp imm
        const auto &Z80STI = MF.getSubtarget<Z80Subtarget>();
        if (!Z80STI.hasLDA())
          break;
        if (DstReg != Z80::HL)
          break;
        // Find preceding instruction
        MachineBasicBlock::iterator PrevIt(MIB.getInstr());
        if (PrevIt == MBB.begin())
          break;
        --PrevIt;
        MachineInstr &PrevMI = *PrevIt;
        if (PrevMI.getOpcode() != Z80::LD16ri)
          break;
        if (PrevMI.getOperand(0).getReg() != Z80::HL)
          break;
        int64_t Imm = PrevMI.getOperand(1).getImm();
        LLVM_DEBUG(dbgs() << "Replacing: "; PrevMI.dump();
                   dbgs() << "     And:  "; MIB->dump();
                   dbgs() << "     With: ");
        // Build LDA16hs_sp before the ADD16as position
        auto NewMI = BuildMI(MBB, MIB.getInstr(), MIB->getDebugLoc(),
                             TII.get(Z80::LDA16hs_sp))
                         .addImm(Imm);
        LLVM_DEBUG(NewMI->dump());
        // Erase both old instructions
        PrevMI.eraseFromParent();
        MIB->eraseFromParent();
        // Clobber HL and F tracking since LDA wrote HL
        clobber<MCRegAliasIterator>(Z80::HL, true);
        assign(Z80::F);
        Changed = true;
        continue;
      }
      }

      if (RegVals[DstReg].matches(DstVal)) {
        LLVM_DEBUG(dbgs() << "Erasing redundant: "; MIB->dump());
        MIB->eraseFromParent();
        reuse(DstReg);
        Changed = true;
        continue;
      }

      bool NeedInc = RegVals[DstReg].matches(DstVal, -1);
      if (NeedInc || RegVals[DstReg].matches(DstVal, +1)) {
        unsigned NewOpc = Z80::INSTRUCTION_LIST_END;
        switch (TRI->getRegSizeInBits(*TRI->getMinimalPhysRegClass(DstReg))) {
        default:
          llvm_unreachable("Unknown register width");
        case 8:
          if (!LiveUnits.available(Z80::F))
            break;
          NewOpc = NeedInc ? Z80::INC8r : Z80::DEC8r;
          break;
        case 16:
          NewOpc = NeedInc ? Z80::INC16r : Z80::DEC16r;
          break;
        case 24:
          NewOpc = NeedInc ? Z80::INC24r : Z80::DEC24r;
          break;
        }
        if (NewOpc != Z80::INSTRUCTION_LIST_END) {
          LLVM_DEBUG(dbgs() << "Replacing: "; MIB->dump();
                     dbgs() << "     With: ");
          MIB->setDesc(TII.get(NewOpc));
          MIB->removeOperand(1);
          MIB.addReg(DstReg, getKillRegState(reuse(DstReg)));
          MIB->addImplicitDefUseOperands(MF);
        }
      }

      // Update killed uses after reuse and before clobbering defs.
      updateDeadOrKilledBy(MIB, &MachineOperand::isKill);

      // Get KnownFlags before clobbering defs.
      KnownFlags KnownFlags =
          getKnownFlags(*MIB, KnownFlagsVal, KnownFlagsMask);

      // Clobber defs.
      for (MachineOperand &MO : MIB->operands()) {
        if (MO.isReg() && MO.isDef() &&
            !(DstReg.isValid() && MO.isImplicit() &&
              TRI->isSuperRegister(DstReg, MO.getReg())))
          clobber<MCRegAliasIterator>(MO.getReg(), true);
        else if (MO.isRegMask())
          for (MCRegister Reg = Z80::NoRegister + 1;
               Reg != Z80::NUM_TARGET_REGS; Reg = Reg + 1)
            if (MO.clobbersPhysReg(Reg))
              assign(Reg);
      }

      // Apply KnownFlags after clobbering defs.
      if (KnownFlags)
        assign(Z80::F, KnownFlags.getKnownVal(KnownFlagsVal),
               KnownFlags.getKnownMask(KnownFlagsMask));

      // Apply known val after clobbering defs.
      if (DstVal.valid()) {
        clobber<MCSuperRegIterator>(DstReg, false);
        RegVals[DstReg] = DstVal;
        for (MCSubRegIndexIterator SRII(DstReg, TRI); SRII.isValid(); ++SRII)
          assign(SRII.getSubReg(), DstVal, SRII.getSubRegIndex());
      }

      // Update Dead Defs after reuse and after clobbering defs.
      updateDeadOrKilledBy(MIB, &MachineOperand::isDead);

      debug(*MIB);
    }
  }
  return Changed;
}

char Z80MachineLateOptimization::ID = 0;
INITIALIZE_PASS(Z80MachineLateOptimization, DEBUG_TYPE,
                "Optimize Z80 machine instrs after regselect", false, false)

FunctionPass *llvm::createZ80MachineLateOptimizationPass() {
  return new Z80MachineLateOptimization();
}
