//===-- Z80InstrInfo.cpp - Z80 Instruction Information --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Z80 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Z80InstrInfo.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Debug.h"
#include <iterator>
using namespace llvm;

#define DEBUG_TYPE "z80-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "Z80GenInstrInfo.inc"

// Pin the vtable to this file.
void Z80InstrInfo::anchor() {}

Z80InstrInfo::Z80InstrInfo(Z80Subtarget &STI)
    : Z80GenInstrInfo((STI.is24Bit() ? Z80::ADJCALLSTACKDOWN24
                                     : Z80::ADJCALLSTACKDOWN16),
                      (STI.is24Bit() ? Z80::ADJCALLSTACKUP24
                                     : Z80::ADJCALLSTACKUP16)),
      Subtarget(STI), RI(STI.getTargetTriple()) {
}

int Z80InstrInfo::getSPAdjust(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case Z80::INC16s:
    return Subtarget.is24Bit() ? 0 : 1;
  case Z80::INC24s:
    return Subtarget.is24Bit() ? 1 : 0;
  case Z80::DEC16s:
    return Subtarget.is24Bit() ? 0 : -1;
  case Z80::DEC24s:
    return Subtarget.is24Bit() ? -1 : 0;
  case Z80::POP16r:
  case Z80::POP16AF:
    return 2;
  case Z80::POP24r:
  case Z80::POP24AF:
    return 3;
  case Z80::PUSH16r:
  case Z80::PUSH16AF:
  case Z80::PEA16o:
    return -2;
  case Z80::PUSH24r:
  case Z80::PUSH24AF:
  case Z80::PEA24o:
    return -3;
  }
  return TargetInstrInfo::getSPAdjust(MI);
}

MachineInstr &Z80InstrInfo::applySPAdjust(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  auto MBBI = std::next(MachineBasicBlock::iterator(MI));
  MachineFunction &MF = *MBB.getParent();
  int Adjust = getSPAdjust(MI);
  // Frame instructions are already handled in BuildStackAdjustment.
  if ((MBBI == MBB.end() || !MBBI->isCFIInstruction()) && !isFrameInstr(MI) &&
      Adjust && MF.needsFrameMoves() &&
      !Subtarget.getFrameLowering()->hasFP(MF))
    BuildMI(MBB, std::next(MachineBasicBlock::iterator(MI)),
            MBB.findDebugLoc(MI), get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(MF.addFrameInst(
            MCCFIInstruction::createAdjustCfaOffset(nullptr, -Adjust)));
  return MI;
}

static bool isIndex(const MachineOperand &MO, const MCRegisterInfo &RI) {
  if (MO.isFI())
    return true;
  if (MO.isReg())
    if (Z80::I8RegClass.contains(MO.getReg()) ||
        Z80::I16RegClass.contains(MO.getReg()) ||
        Z80::I24RegClass.contains(MO.getReg()))
      return true;
  return false;
}

unsigned Z80InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Size = 0;
  auto TSFlags = MI.getDesc().TSFlags;

  // compute the prefix and offset
  bool IdxPre = false;
  bool CBPre = TSFlags & Z80II::CBPre;
  bool EDPre = TSFlags & Z80II::EDPre;
  bool HasOff = TSFlags & Z80II::HasOff;
  assert(!(CBPre && EDPre));
  for (unsigned OpIdx = 0; OpIdx != 2; ++OpIdx) {
    if (!(TSFlags & Z80II::Idx0Pre << OpIdx) ||
        !isIndex(MI.getOperand(OpIdx), getRegisterInfo()))
      continue;
    IdxPre = true;
    // index prefix cannot be combined with ED prefix
    EDPre = false;
    // (ix) gains an offset
    if (MI.getDesc().OpInfo[OpIdx].OperandType == MCOI::OPERAND_MEMORY)
      HasOff = true;
  }

  // suffix byte
  switch (TSFlags & Z80II::ModeMask) {
  case Z80II::AnyMode:
  case Z80II::CurMode:
    break;
  case Z80II::Z80Mode:
    Size += !Subtarget.is16Bit();
    break;
  case Z80II::EZ80Mode:
    Size += !Subtarget.is24Bit();
    break;
  }

  // prefix byte(s)
  Size += IdxPre + CBPre + EDPre;

  // opcode byte
  Size += 1;

  // immediate byte(s)
  if (TSFlags & Z80II::HasImm)
    switch (TSFlags & Z80II::ModeMask) {
    case Z80II::AnyMode:
      Size += 1;
      break;
    case Z80II::CurMode:
      Size += 2 + Subtarget.is24Bit();;
      break;
    case Z80II::Z80Mode:
      Size += 2;
      break;
    case Z80II::EZ80Mode:
      Size += 3;
      break;
    }

  // offset byte
  Size += HasOff;

  return Size;
}

Z80::CondCode Z80::GetBranchConditionForPredicate(CmpInst::Predicate Pred,
                                                  bool &IsSigned,
                                                  bool &IsSwapped,
                                                  bool &IsConst) {
  IsSigned = CmpInst::isSigned(Pred);
  IsSwapped = IsConst = false;
  switch (Pred) {
  case CmpInst::FCMP_FALSE:
  case CmpInst::FCMP_UNO:
    IsConst = true;
    return Z80::COND_C;
  case CmpInst::FCMP_TRUE:
  case CmpInst::FCMP_ORD:
    IsConst = true;
    return Z80::COND_NC;
  case CmpInst::FCMP_OEQ:
  case CmpInst::FCMP_UEQ:
  case CmpInst::ICMP_EQ:
    return Z80::COND_Z;
  case CmpInst::FCMP_ONE:
  case CmpInst::FCMP_UNE:
  case CmpInst::ICMP_NE:
    return Z80::COND_NZ;
  case CmpInst::ICMP_UGT:
    IsSwapped = true;
    LLVM_FALLTHROUGH;
  case CmpInst::ICMP_ULT:
    return Z80::COND_C;
  case CmpInst::ICMP_ULE:
    IsSwapped = true;
    LLVM_FALLTHROUGH;
  case CmpInst::ICMP_UGE:
    return Z80::COND_NC;
  case CmpInst::FCMP_OGT:
  case CmpInst::FCMP_UGT:
  case CmpInst::ICMP_SGT:
    IsSwapped = true;
    LLVM_FALLTHROUGH;
  case CmpInst::FCMP_OLT:
  case CmpInst::FCMP_ULT:
  case CmpInst::ICMP_SLT:
    return Z80::COND_M;
  case CmpInst::FCMP_OLE:
  case CmpInst::FCMP_ULE:
  case CmpInst::ICMP_SLE:
    IsSwapped = true;
    LLVM_FALLTHROUGH;
  case CmpInst::FCMP_OGE:
  case CmpInst::FCMP_UGE:
  case CmpInst::ICMP_SGE:
    return Z80::COND_P;
  default:
    llvm_unreachable("Unknown predicate");
  }
}

/// Return the inverse of the specified condition,
/// e.g. turning COND_E to COND_NE.
Z80::CondCode Z80::GetOppositeBranchCondition(Z80::CondCode CC) {
  return CC == Z80::COND_INVALID ? CC : Z80::CondCode(CC ^ 1);
}

Z80::CondCode Z80::parseConstraintCode(StringRef Constraint) {
  return StringSwitch<Z80::CondCode>(Constraint)
      .Case("{@ccnz}", Z80::COND_NZ)
      .Case("{@ccz}", Z80::COND_Z)
      .Case("{@ccnc}", Z80::COND_NC)
      .Case("{@ccc}", Z80::COND_C)
      .Case("{@ccpo}", Z80::COND_PO)
      .Case("{@ccpe}", Z80::COND_PE)
      .Case("{@ccp}", Z80::COND_P)
      .Case("{@ccm}", Z80::COND_M)
      .Default(Z80::COND_INVALID);
}

bool Z80InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end(), UnCondBrIter = I;
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!I->isBranch())
      return true;

    // Cannot handle branches that don't branch to a block.
    if (!I->getOperand(0).isMBB())
      return true;

    // Handle unconditional branches.
    if (I->getNumExplicitOperands() == 1 && I->getOpcode() != Z80::DJNZ) {
      UnCondBrIter = I;

      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      while (std::next(I) != MBB.end())
        std::next(I)->eraseFromParent();
      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        UnCondBrIter = I;
        continue;
      }

      // TBB is used to indicate the unconditional destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    assert(I->getNumExplicitOperands() ==
               (I->getOpcode() == Z80::DJNZ ? 1 : 2) &&
           "Invalid conditional branch");
    Z80::CondCode CC = I->getNumExplicitOperands() < 2
                           ? Z80::COND_INVALID
                           : Z80::CondCode(I->getOperand(1).getImm());

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      MachineBasicBlock *TargetBB = I->getOperand(0).getMBB();
      if (CC != Z80::COND_INVALID && AllowModify && UnCondBrIter != MBB.end() &&
          MBB.isLayoutSuccessor(TargetBB)) {
        // If we can modify the code and it ends in something like:
        //
        //     jCC L1
        //     jmp L2
        //   L1:
        //     ...
        //   L2:
        //
        // Then we can change this to:
        //
        //     jnCC L2
        //   L1:
        //     ...
        //   L2:
        //
        // Which is a bit more efficient.
        // We conditionally jump to the fall-through block.
        CC = GetOppositeBranchCondition(CC);
        MachineBasicBlock::iterator OldInst = I;

        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(Z80::JQCC))
          .addMBB(UnCondBrIter->getOperand(0).getMBB()).addImm(CC);
        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(Z80::JQ))
          .addMBB(TargetBB);

        OldInst->eraseFromParent();
        UnCondBrIter->eraseFromParent();

        // Restart the analysis.
        UnCondBrIter = MBB.end();
        I = MBB.end();
        continue;
      }

      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(CC));
      continue;
    }

    return true;
  }

  return false;
}

unsigned Z80InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.end();
  int Bytes = 0;
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugValue())
      continue;
    if (!I->isBranch())
      break;
    // Remove the branch.
    Bytes += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  if (BytesRemoved)
    *BytesRemoved = Bytes;
  return Count;
}

unsigned Z80InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL,
                                    int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");
  assert(Cond.size() <= 1 && "Z80 branch conditions have one component!");
  int Bytes = 0;
  unsigned Count = 0;

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    Bytes += getInstSizeInBytes(*BuildMI(&MBB, DL, get(Z80::JQ)).addMBB(TBB));
    ++Count;
  } else {
    // Conditional branch.
    Bytes += getInstSizeInBytes(
        *BuildMI(&MBB, DL, get(Z80::JQCC)).addMBB(TBB).add(Cond[0]));
    ++Count;

    // If FBB is null, it is implied to be a fall-through block.
    if (FBB) {
      // Two-way Conditional branch. Insert the second branch.
      Bytes += getInstSizeInBytes(*BuildMI(&MBB, DL, get(Z80::JQ)).addMBB(FBB));
      ++Count;
    }
  }

  if (BytesAdded)
    *BytesAdded = Bytes;
  return Count;
}

bool Z80InstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid Z80 branch condition!");
  auto CC = Z80::CondCode(Cond[0].getImm());
  if (CC == Z80::COND_INVALID)
    return true;
  Cond[0].setImm(GetOppositeBranchCondition(CC));
  return false;
}

bool Z80::splitReg(
    unsigned ByteSize, unsigned Opc8, unsigned Opc16, unsigned Opc24,
    unsigned &RC, unsigned &LoOpc, unsigned &LoIdx, unsigned &HiOpc,
    unsigned &HiIdx, unsigned &HiOff, bool Has16BitEZ80Ops) {
  switch (ByteSize) {
  default: llvm_unreachable("Unexpected Size!");
  case 1:
    RC = Z80::R8RegClassID;
    LoOpc = HiOpc = Opc8;
    LoIdx = HiIdx = Z80::NoSubRegister;
    HiOff = 0;
    return false;
  case 2:
    if (Has16BitEZ80Ops) {
      RC = Z80::R16RegClassID;
      LoOpc = HiOpc = Opc16;
      LoIdx = HiIdx = Z80::NoSubRegister;
      HiOff = 0;
      return false;
    }
    RC = Z80::R16RegClassID;
    LoOpc = HiOpc = Opc8;
    LoIdx = Z80::sub_low;
    HiIdx = Z80::sub_high;
    HiOff = 1;
    return true;
  case 3:
    // Legalization should have taken care of this if we don't have eZ80 ops
    //assert(Is24Bit && HasEZ80Ops && "Need eZ80 24-bit load/store");
    RC = Z80::R24RegClassID;
    LoOpc = HiOpc = Opc24;
    LoIdx = HiIdx = Z80::NoSubRegister;
    HiOff = 0;
    return false;
  }
}

bool Z80InstrInfo::canExchange(MCRegister RegA, MCRegister RegB) const {
  // The only regs that can be directly exchanged are DE and HL, in any order.
  bool DE = false, HL = false;
  for (MCRegister Reg : {RegA, RegB}) {
    if (RI.isSubRegisterEq(Z80::UDE, Reg))
      DE = true;
    else if (RI.isSubRegisterEq(Z80::UHL, Reg))
      HL = true;
  }
  return DE && HL;
}

void Z80InstrInfo::copyRegister(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                const DebugLoc &DL, Register DstReg,
                                Register SrcReg, bool KillSrc) const {
  if (DstReg.isPhysical() && DstReg.isPhysical()) {
    copyPhysReg(MBB, MI, DL, DstReg, SrcReg, KillSrc);
    return;
  }
  // Identity copy.
  if (DstReg == SrcReg)
    return;
  BuildMI(MBB, MI, DL, get(TargetOpcode::COPY), DstReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void Z80InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, MCRegister DstReg,
                               MCRegister SrcReg, bool KillSrc) const {
  assert(MCRegister::isPhysicalRegister(DstReg) &&
         MCRegister::isPhysicalRegister(SrcReg) &&
         "Expected physical registers");
  LLVM_DEBUG(dbgs() << RI.getName(DstReg) << " = " << RI.getName(SrcReg)
                    << '\n');
  /*for (auto Regs : {std::make_pair(DstReg, &SrcReg),
                    std::make_pair(SrcReg, &DstReg)}) {
    if (Z80::R8RegClass.contains(Regs.first) &&
        (Z80::R16RegClass.contains(*Regs.second) ||
         Z80::R24RegClass.contains(*Regs.second)))
      *Regs.second = RI.getSubReg(*Regs.second, Z80::sub_low);
  }*/
  // Identity copy.
  if (DstReg == SrcReg)
    return;
  bool Is24Bit = Subtarget.is24Bit();
  if (Z80::R8RegClass.contains(DstReg, SrcReg)) {
    // Byte copy.
    if (Z80::G8RegClass.contains(DstReg, SrcReg)) {
      // Neither are index registers.
      BuildMI(MBB, MI, DL, get(Z80::LD8gg), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    } else if (Z80::I8RegClass.contains(DstReg, SrcReg)) {
      assert(Subtarget.hasIndexHalfRegs() && "Need  index half registers");
      // Both are index registers.
      if (Z80::X8RegClass.contains(DstReg, SrcReg)) {
        BuildMI(MBB, MI, DL, get(Z80::LD8xx), DstReg)
          .addReg(SrcReg, getKillRegState(KillSrc));
      } else if (Z80::Y8RegClass.contains(DstReg, SrcReg)) {
        BuildMI(MBB, MI, DL, get(Z80::LD8yy), DstReg)
          .addReg(SrcReg, getKillRegState(KillSrc));
      } else {
        // We are copying between different index registers, so we need to use
        // an intermediate register.
        applySPAdjust(
            *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
            .findRegisterUseOperand(Z80::AF)->setIsUndef();
        BuildMI(MBB, MI, DL, get(Z80::X8RegClass.contains(SrcReg) ? Z80::LD8xx
                                                                  : Z80::LD8yy),
                Z80::A).addReg(SrcReg, getKillRegState(KillSrc));
        BuildMI(MBB, MI, DL, get(Z80::X8RegClass.contains(DstReg) ? Z80::LD8xx
                                                                  : Z80::LD8yy),
                DstReg).addReg(Z80::A);
        applySPAdjust(
            *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
      }
    } else {
      assert(Subtarget.hasIndexHalfRegs() && "Need  index half registers");
      // Only one is an index register, which isn't directly possible if one of
      // them is from HL.  If so, surround with EX DE,HL and use DE instead.
      bool NeedEX = false;
      for (MCRegister *Reg : {&DstReg, &SrcReg}) {
        switch (*Reg) {
        case Z80::H: *Reg = Z80::D; NeedEX = true; break;
        case Z80::L: *Reg = Z80::E; NeedEX = true; break;
        }
      }
      unsigned ExOpc = Is24Bit ? Z80::EX24DE : Z80::EX16DE;
      if (NeedEX) {
        // If the prev instr was an EX DE,HL, just kill it.
        if (MI == MBB.begin() || std::prev(MI)->getOpcode() != ExOpc) {
          auto Ex = BuildMI(MBB, MI, DL, get(ExOpc));
          Ex->findRegisterUseOperand(Is24Bit ? Z80::UDE : Z80::DE)
              ->setIsUndef();
          Ex->findRegisterUseOperand(Is24Bit ? Z80::UHL : Z80::HL)
              ->setIsUndef();
        } else
          std::prev(MI)->eraseFromParent();
      }
      BuildMI(MBB, MI, DL,
              get(Z80::X8RegClass.contains(DstReg, SrcReg) ? Z80::LD8xx
                                                           : Z80::LD8yy),
              DstReg).addReg(SrcReg, getKillRegState(KillSrc));
      if (NeedEX)
        BuildMI(MBB, MI, DL, get(ExOpc))
          ->findRegisterUseOperand(Is24Bit ? Z80::UHL : Z80::HL)->setIsUndef();
    }
    return;
  }
  // Specialized byte copy.
  if (DstReg == Z80::F) {
    // Copies to F.
    bool NeedEX = false;
    switch (SrcReg) {
    case Z80::H: SrcReg = Z80::D; NeedEX = true; break;
    case Z80::L: SrcReg = Z80::E; NeedEX = true; break;
    }
    Register TempReg = Is24Bit ? Z80::UHL : Z80::HL;
    if (NeedEX)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(Is24Bit ? Z80::UDE : Z80::DE, RegState::ImplicitDefine)
          .addReg(TempReg, RegState::ImplicitDefine);
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
             .addReg(TempReg, RegState::Undef));
    copyPhysReg(MBB, MI, DL, Z80::L, SrcReg, KillSrc);
    BuildMI(MBB, MI, DL, get(TargetOpcode::COPY), Z80::H)
        .addReg(Z80::A, RegState::Undef); // Preserve A
    BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa), TempReg)
        .addReg(TempReg, RegState::Undef);
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    if (NeedEX)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(Is24Bit ? Z80::UDE : Z80::DE, RegState::ImplicitDefine)
          .addReg(TempReg, RegState::ImplicitDefine);
    return;
  }
  if (SrcReg == Z80::F) {
    // Copies from F.
    bool NeedEX = false;
    switch (DstReg) {
    case Z80::H: DstReg = Z80::D; NeedEX = true; break;
    case Z80::L: DstReg = Z80::E; NeedEX = true; break;
    }
    Register TempReg = Is24Bit ? Z80::UHL : Z80::HL;
    if (NeedEX)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(Is24Bit ? Z80::UDE : Z80::DE, RegState::ImplicitDefine)
          .addReg(TempReg, RegState::ImplicitDefine);
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
        .findRegisterUseOperand(Z80::AF)->setIsUndef();
    BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa), TempReg)
        .addReg(TempReg, RegState::Undef);
    copyPhysReg(MBB, MI, DL, DstReg, Z80::L, KillSrc);
    applySPAdjust(*BuildMI(MBB, MI, DL,
                           get(Is24Bit ? Z80::POP24r : Z80::POP16r), TempReg));
    if (NeedEX)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(Is24Bit ? Z80::UDE : Z80::DE, RegState::ImplicitDefine)
          .addReg(TempReg, RegState::ImplicitDefine);
    return;
  }
  // Specialized word copy.
  if (DstReg == Z80::SPS || DstReg == Z80::SPL) {
    // Copies to SP.
    assert((Z80::A16RegClass.contains(SrcReg) || SrcReg == Z80::DE ||
            Z80::A24RegClass.contains(SrcReg) || SrcReg == Z80::UDE) &&
           "Unimplemented");
    Is24Bit |= DstReg == Z80::SPL;
    MCRegister ExReg;
    if (SrcReg == Z80::DE || SrcReg == Z80::UDE) {
      ExReg = SrcReg;
      SrcReg = SrcReg == Z80::DE ? Z80::HL : Z80::UHL;
    }
    if (ExReg)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(ExReg, RegState::ImplicitDefine)
          .addReg(SrcReg, RegState::ImplicitKill);
    BuildMI(MBB, MI, DL, get(DstReg == Z80::SPL ? Z80::LD24sa : Z80::LD16sa))
        .addReg(SrcReg, getKillRegState(KillSrc));
    if (ExReg)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(ExReg, RegState::ImplicitDefine)
          .addReg(SrcReg, RegState::ImplicitDefine);
    return;
  }
  if (SrcReg == Z80::SPL || SrcReg == Z80::SPS) {
    // Copies from SP.
    Is24Bit |= SrcReg == Z80::SPL;
    MCRegister PopReg, ExReg;
    if (DstReg == Z80::UBC || DstReg == Z80::BC) {
      PopReg = DstReg;
      DstReg = Is24Bit ? Z80::UHL : Z80::HL;
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
               .addReg(DstReg));
    } else if (DstReg == Z80::UDE || DstReg == Z80::DE) {
      ExReg = DstReg;
      DstReg = Is24Bit ? Z80::UHL : Z80::HL;
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(ExReg, RegState::ImplicitDefine)
          .addReg(DstReg, RegState::ImplicitKill);
    }
    BuildMI(MBB, MI, DL, get(Subtarget.is24Bit() ? Z80::LD24ri : Z80::LD16ri),
            DstReg).addImm(PopReg ? Is24Bit ? 3 : 2 : 0);
    BuildMI(MBB, MI, DL, get(SrcReg == Z80::SPL ? Z80::ADD24as : Z80::ADD16as),
            DstReg).addReg(DstReg)->addRegisterDead(Z80::F, &getRegisterInfo());
    if (PopReg) {
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa), DstReg)
          .addReg(DstReg);
      applySPAdjust(*BuildMI(MBB, MI, DL,
                             get(Is24Bit ? Z80::POP24r : Z80::POP16r), PopReg));
    } else if (ExReg)
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24DE : Z80::EX16DE))
          .addReg(ExReg, RegState::ImplicitDefine)
          .addReg(DstReg, RegState::Implicit | getRegState(MI->getOperand(0)));
    return;
  }
  Is24Bit = Z80::R24RegClass.contains(DstReg, SrcReg);
  if (Is24Bit == Subtarget.is24Bit()) {
    // Special case DE/HL = HL/DE<kill> as EX DE,HL.
    if (KillSrc && canExchange(DstReg, SrcReg)) {
      MachineInstrBuilder MIB = BuildMI(MBB, MI, DL,
                                        get(Is24Bit ? Z80::EX24DE
                                                    : Z80::EX16DE));
      MIB->findRegisterUseOperand(SrcReg)->setIsKill();
      MIB->findRegisterDefOperand(SrcReg)->setIsDead();
      MIB->findRegisterUseOperand(DstReg)->setIsUndef();
      return;
    }
    bool IsSrcIndexReg = Z80::I16RegClass.contains(SrcReg) ||
                         Z80::I24RegClass.contains(SrcReg);
    // Special case copies from index registers when we have eZ80 ops.
    if (Subtarget.hasEZ80Ops() && IsSrcIndexReg) {
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::LEA24ro : Z80::LEA16ro), DstReg)
        .addReg(SrcReg, getKillRegState(KillSrc)).addImm(0);
      return;
    }
    // If both are 24-bit then the upper byte needs to be preserved.
    // Otherwise copies of index registers may need to use this method if:
    // - We are optimizing for size and exactly one reg is an index reg because
    //     PUSH SrcReg \ POP DstReg is (2 + NumIndexRegs) bytes but slower
    //     LD DstRegLo,SrcRegLo \ LD DstRegHi,SrcRegHi is 4 bytes but faster
    // - We don't have undocumented half index copies
    bool IsDstIndexReg = Z80::I16RegClass.contains(DstReg) ||
                         Z80::I24RegClass.contains(DstReg);
    unsigned NumIndexRegs = IsSrcIndexReg + IsDstIndexReg;
    bool OptSize = MBB.getParent()->getFunction().hasOptSize();
    if (Is24Bit || (NumIndexRegs == 1 && OptSize) ||
        (NumIndexRegs && !Subtarget.hasIndexHalfRegs())) {
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
               .addReg(SrcReg, getKillRegState(KillSrc)));
      applySPAdjust(*BuildMI(MBB, MI, DL,
                             get(Is24Bit ? Z80::POP24r : Z80::POP16r), DstReg));
      return;
    }
  }
  // Otherwise, implement as two copies. A 16-bit copy should copy high and low
  // 8 bits separately.
  assert(Z80::R16RegClass.contains(DstReg, SrcReg) && "Unknown copy width");
  unsigned SubLo = Z80::sub_low;
  unsigned SubHi = Z80::sub_high;
  MCRegister DstLoReg = RI.getSubReg(DstReg, SubLo);
  MCRegister SrcLoReg = RI.getSubReg(SrcReg, SubLo);
  MCRegister DstHiReg = RI.getSubReg(DstReg, SubHi);
  MCRegister SrcHiReg = RI.getSubReg(SrcReg, SubHi);
/*bool DstLoSrcHiOverlap = RI.regsOverlap(DstLoReg, SrcHiReg);
  bool SrcLoDstHiOverlap = RI.regsOverlap(SrcLoReg, DstHiReg);
  if (DstLoSrcHiOverlap && SrcLoDstHiOverlap) {
    assert(KillSrc &&
           "Both parts of SrcReg and DstReg overlap but not killing source!");
    // e.g. EUHL = LUDE so just swap the operands
    MCRegister OtherReg;
    if (canExchange(DstLoReg, SrcLoReg)) {
      BuildMI(MBB, MI, DL, get(Subtarget.is24Bit() ? Z80::EX24DE : Z80::EX16DE))
          .addReg(DstReg, RegState::ImplicitDefine)
          .addReg(SrcReg, RegState::ImplicitKill);
    } else if ((OtherReg = DstLoReg, RI.isSubRegisterEq(Z80::UHL, SrcLoReg)) ||
               (OtherReg = SrcLoReg, RI.isSubRegisterEq(Z80::UHL, DstLoReg))) {
      applySPAdjust(
          BuildMI(MBB, MI, DL,
                  get(Subtarget.is24Bit() ? Z80::PUSH24r : Z80::PUSH16r))
              .addReg(OtherReg, RegState::Kill));
      BuildMI(MBB, MI, DL,
              get(Subtarget.is24Bit() ? Z80::EX24sa : Z80::EX16sa));
      applySPAdjust(BuildMI(
          MBB, MI, DL, get(Subtarget.is24Bit() ? Z80::POP24r : Z80::POP16r),
          OtherReg));
    } else {
      applySPAdjust(
          BuildMI(MBB, MI, DL,
                  get(Subtarget.is24Bit() ? Z80::PUSH24r : Z80::PUSH16r))
              .addReg(SrcLoReg, RegState::Kill));
      applySPAdjust(
          BuildMI(MBB, MI, DL,
                  get(Subtarget.is24Bit() ? Z80::PUSH24r : Z80::PUSH16r))
              .addReg(DstLoReg, RegState::Kill));
      applySPAdjust(BuildMI(
          MBB, MI, DL, get(Subtarget.is24Bit() ? Z80::POP24r : Z80::POP16r),
          SrcLoReg));
      applySPAdjust(BuildMI(
          MBB, MI, DL, get(Subtarget.is24Bit() ? Z80::POP24r : Z80::POP16r),
          DstLoReg));
    }
    // Check if top needs to be moved (e.g. EUHL = HUDE).
    unsigned DstHiIdx = RI.getSubRegIndex(SrcLoReg, DstHiReg);
    unsigned SrcHiIdx = RI.getSubRegIndex(DstLoReg, SrcHiReg);
    if (DstHiIdx != SrcHiIdx)
      copyPhysReg(MBB, MI, DL, DstHiReg,
                  RI.getSubReg(DstLoReg, SrcHiIdx), KillSrc);
  } else if (DstLoSrcHiOverlap) {
    // Copy out SrcHi before SrcLo overwrites it.
    copyPhysReg(MBB, MI, DL, DstHiReg, SrcHiReg, KillSrc);
    copyPhysReg(MBB, MI, DL, DstLoReg, SrcLoReg, KillSrc);
  } else*/ {
    // If SrcLoDstHiOverlap then copy out SrcLo before SrcHi overwrites it,
    // otherwise the order doesn't matter.
    copyPhysReg(MBB, MI, DL, DstLoReg, SrcLoReg, KillSrc);
    copyPhysReg(MBB, MI, DL, DstHiReg, SrcHiReg, KillSrc);
  }
  --MI;
  MI->addRegisterDefined(DstReg, &RI);
  if (KillSrc)
    MI->addRegisterKilled(SrcReg, &RI, true);
}

void Z80InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MI,
                                       Register SrcReg, bool IsKill, int FI,
                                       const TargetRegisterClass *TRC,
                                       const TargetRegisterInfo *TRI) const {
  const DebugLoc &DL = MBB.findDebugLoc(MI);
  bool Is24Bit = Subtarget.is24Bit();

  // Special cases
  switch (SrcReg) {
  case Z80::F: {
    Register TempReg = Is24Bit ? Z80::UHL : Z80::HL;
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
        .findRegisterUseOperand(Z80::AF)->setIsUndef();
    BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa), TempReg)
        .addReg(TempReg, RegState::Undef);
    storeRegToStackSlot(MBB, MI, Z80::L, true, FI, &Z80::R8RegClass, TRI);
    applySPAdjust(*BuildMI(MBB, MI, DL,
                           get(Is24Bit ? Z80::POP24r : Z80::POP16r), TempReg));
    return;
  }
  }

  unsigned Opc;
  switch (TRI->getSpillSize(*TRC)) {
  default:
    llvm_unreachable("Unexpected regclass size");
  case 1:
    Opc = Z80::LD8or;
    break;
  case 2:
    Opc = Subtarget.has16BitEZ80Ops() ? Z80::LD16or : Z80::LD88or;
    break;
  case 3:
    assert(Is24Bit && "Only 24-bit should have 3 byte stack slots");
    Opc = Z80::LD24or;
    break;
  }
  BuildMI(MBB, MI, DL, get(Opc))
      .addFrameIndex(FI).addImm(0).addReg(SrcReg, getKillRegState(IsKill));
}

void Z80InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DstReg, int FI,
                                        const TargetRegisterClass *TRC,
                                        const TargetRegisterInfo *TRI) const {
  const DebugLoc &DL = MBB.findDebugLoc(MI);
  bool Is24Bit = Subtarget.is24Bit();

  // Special cases
  switch (DstReg) {
  case Z80::F: {
    Register TempReg = Is24Bit ? Z80::UHL : Z80::HL;
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
             .addReg(TempReg, RegState::Undef));
    loadRegFromStackSlot(MBB, MI, Z80::L, FI, &Z80::R8RegClass, TRI);
    BuildMI(MBB, MI, DL, get(TargetOpcode::COPY), Z80::H)
        .addReg(Z80::A, RegState::Undef); // Preserve A
    BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa), TempReg)
        .addReg(TempReg, RegState::Undef);
    applySPAdjust(
        *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    return;
  }
  }

  unsigned Opc;
  switch (TRI->getSpillSize(*TRC)) {
  default:
    llvm_unreachable("Unexpected regclass size");
  case 1:
    Opc = Z80::LD8ro;
    break;
  case 2:
    if (!Is24Bit) {
      Opc = Subtarget.has16BitEZ80Ops() ? Z80::LD16ro : Z80::LD88ro;
      break;
    }
    TRC = &Z80::R24RegClass;
    DstReg = TRI->getMatchingSuperReg(DstReg, Z80::sub_short, TRC);
    LLVM_FALLTHROUGH;
  case 3:
    assert(Is24Bit && "Only 24-bit should have 3 byte stack slots");
    Opc = Z80::LD24ro;
    if (DstReg.isPhysical())
      if (auto SuperReg = TRI->getMatchingSuperReg(DstReg, Z80::sub_short,
                                                   &Z80::R24RegClass))
        DstReg = SuperReg;
    break;
  }
  BuildMI(MBB, MI, DL, get(Opc), DstReg).addFrameIndex(FI).addImm(0);
}

/// Return true and the FrameIndex if the specified
/// operand and follow operands form a reference to the stack frame.
bool Z80InstrInfo::isFrameOperand(const MachineInstr &MI, unsigned int Op,
                                  int &FrameIndex) const {
  if (MI.getOperand(Op).isFI() &&
      MI.getOperand(Op + 1).isImm() && MI.getOperand(Op + 1).getImm() == 0) {
    FrameIndex = MI.getOperand(Op).getIndex();
    return true;
  }
  return false;
}

static bool isFrameLoadOpcode(int Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Z80::LD8ro:
  case Z80::LD8go:
  case Z80::LD16ro:
  case Z80::LD88ro:
  case Z80::LD24ro:
    return true;
  }
}
unsigned Z80InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  if (isFrameLoadOpcode(MI.getOpcode()) && !MI.getOperand(0).getSubReg() &&
      isFrameOperand(MI, 1, FrameIndex))
    return MI.getOperand(0).getReg();
  return 0;
}

static Register scavengeOrCreateRegister(const TargetRegisterClass *RC,
                                         MachineRegisterInfo &MRI,
                                         MachineBasicBlock::iterator II,
                                         RegScavenger *RS = nullptr,
                                         int SPAdj = 0,
                                         bool AllowSpill = true) {
  return RS ? RS->scavengeRegister(RC, II, SPAdj, AllowSpill)
            : MRI.createVirtualRegister(RC);
}

static Register findUnusedOrCreateRegister(const TargetRegisterClass *RC,
                                           MachineRegisterInfo &MRI,
                                           RegScavenger *RS = nullptr) {
  return RS ? RS->FindUnusedReg(RC) : MRI.createVirtualRegister(RC);
}
static Register createIfVirtual(Register Reg, MachineRegisterInfo &MRI) {
  return Reg.isPhysical() ? Reg
                          : MRI.createVirtualRegister(MRI.getRegClass(Reg));
}
void Z80InstrInfo::rewriteFrameIndex(MachineInstr &MI, unsigned FIOperandNum,
                                     Register BaseReg, int64_t Offset,
                                     RegScavenger *RS, int SPAdj) const {
  MachineBasicBlock::iterator II(MI);
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Z80RegisterInfo &TRI = getRegisterInfo();
  DebugLoc DL = MI.getDebugLoc();
  bool Is24Bit = Subtarget.is24Bit();
  int64_t NewOffset = Offset + TRI.getFrameIndexInstrOffset(&MI, FIOperandNum);

  unsigned Opc = MI.getOpcode();
  bool IllegalLEA = Opc == Z80::LEA16ro && !Subtarget.hasEZ80Ops();
  if (TRI.isFrameOffsetLegal(&MI, BaseReg, Offset) && !IllegalLEA) {
    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
    if (!NewOffset && (Opc == Z80::PEA24o || Opc == Z80::PEA16o)) {
      MI.setDesc(get(Opc == Z80::PEA24o ? Z80::PUSH24r : Z80::PUSH16r));
      MI.removeOperand(FIOperandNum + 1);
      return;
    }
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(NewOffset);
    return;
  }

  bool SaveFlags = RS && RS->isRegUsed(Z80::F);
  Register OffsetReg = scavengeOrCreateRegister(
      Is24Bit ? &Z80::O24RegClass : &Z80::O16RegClass, MRI, II, RS, SPAdj);
  if ((Opc == Z80::LEA24ro &&
       Z80::A24RegClass.contains(MI.getOperand(0).getReg())) ||
      (Opc == Z80::LEA16ro &&
       Z80::A16RegClass.contains(MI.getOperand(0).getReg()))) {
    Register Op0Reg = MI.getOperand(0).getReg();
    BuildMI(MBB, II, DL, get(Is24Bit ? Z80::LD24ri : Z80::LD16ri), OffsetReg)
        .addImm(NewOffset);
    copyRegister(MBB, II, DL, Op0Reg, BaseReg);
    if (SaveFlags)
      applySPAdjust(
          *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
          .findRegisterUseOperand(Z80::AF)->setIsUndef();
    BuildMI(MBB, II, DL, get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao), Op0Reg)
        .addReg(Op0Reg).addReg(OffsetReg, RegState::Kill)
        ->addRegisterDead(Z80::F, &TRI);
    if (SaveFlags)
      applySPAdjust(
          *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    MI.eraseFromParent();
    return;
  }

  if (Register ScratchReg = findUnusedOrCreateRegister(
          Is24Bit ? &Z80::A24RegClass : &Z80::A16RegClass, MRI, RS)) {
    BuildMI(MBB, II, DL, get(Is24Bit ? Z80::LD24ri : Z80::LD16ri), OffsetReg)
        .addImm(NewOffset);
    copyRegister(MBB, II, DL, ScratchReg, BaseReg);
    if (SaveFlags)
      applySPAdjust(
          *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
          .findRegisterUseOperand(Z80::AF)->setIsUndef();
    Register TempReg = createIfVirtual(ScratchReg, MRI);
    BuildMI(MBB, II, DL, get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao), TempReg)
        .addReg(ScratchReg).addReg(OffsetReg, RegState::Kill)
        ->addRegisterDead(Z80::F, &TRI);
    if (SaveFlags)
      applySPAdjust(
          *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    if (IllegalLEA) {
      copyRegister(MBB, II, DL, MI.getOperand(0).getReg(), TempReg);
      MI.eraseFromParent();
      return;
    }
    MI.getOperand(FIOperandNum).ChangeToRegister(TempReg, false);
    if ((Is24Bit ? Z80::I24RegClass : Z80::I16RegClass).contains(TempReg)) {
      MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
      return;
    }
    switch (Opc) {
    default: llvm_unreachable("Unexpected opcode!");
    case Z80::LD24ro:  Opc = Z80::LD24rp;  break;
    case Z80::LD16ro:  Opc = Z80::LD16rp;  break;
    case Z80::LD88ro:  Opc = Z80::LD88rp;  break;
    case Z80::LD8ro:   Opc = Z80::LD8rp;   break;
    case Z80::LD8go:   Opc = Z80::LD8gp;   break;
    case Z80::LD24or:  Opc = Z80::LD24pr;  break;
    case Z80::LD16or:  Opc = Z80::LD16pr;  break;
    case Z80::LD88or:  Opc = Z80::LD88pr;  break;
    case Z80::LD8or:   Opc = Z80::LD8pr;   break;
    case Z80::LD8og:   Opc = Z80::LD8pg;   break;
    case Z80::LD8oi:   Opc = Z80::LD8pi;   break;
    case Z80::PEA24o:  Opc = Z80::PUSH24r; break;
    case Z80::PEA16o:  Opc = Z80::PUSH16r; break;
    case Z80::RLC8o:   Opc = Z80::RLC8p;   break;
    case Z80::RRC8o:   Opc = Z80::RRC8p;   break;
    case Z80::RL8o:    Opc = Z80::RL8p;    break;
    case Z80::RR8o:    Opc = Z80::RR8p;    break;
    case Z80::SLA8o:   Opc = Z80::SLA8p;   break;
    case Z80::SRA8o:   Opc = Z80::SRA8p;   break;
    case Z80::SRL8o:   Opc = Z80::SRL8p;   break;
    case Z80::BIT8ob:  Opc = Z80::BIT8pb;  break;
    case Z80::RES8ob:  Opc = Z80::RES8pb;  break;
    case Z80::SET8ob:  Opc = Z80::SET8pb;  break;
    case Z80::INC8o:   Opc = Z80::INC8p;   break;
    case Z80::DEC8o:   Opc = Z80::DEC8p;   break;
    case Z80::ADD8ao:  Opc = Z80::ADD8ap;  break;
    case Z80::ADC8ao:  Opc = Z80::ADC8ap;  break;
    case Z80::SUB8ao:  Opc = Z80::SUB8ap;  break;
    case Z80::SBC8ao:  Opc = Z80::SBC8ap;  break;
    case Z80::AND8ao:  Opc = Z80::AND8ap;  break;
    case Z80::XOR8ao:  Opc = Z80::XOR8ap;  break;
    case Z80::OR8ao:   Opc = Z80::OR8ap;   break;
    case Z80::CP8ao:   Opc = Z80::CP8ap;   break;
    case Z80::LEA24ro: case Z80::LEA16ro:
      copyRegister(MBB, ++II, DL, MI.getOperand(0).getReg(),
                   MI.getOperand(FIOperandNum).getReg());
      MI.eraseFromParent();
      return;
    }
    MI.setDesc(get(Opc));
    MI.removeOperand(FIOperandNum + 1);
    return;
  }

  applySPAdjust(
      *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
           .addReg(BaseReg));
  BuildMI(MBB, II, DL, get(Is24Bit ? Z80::LD24ri : Z80::LD16ri), OffsetReg)
      .addImm(NewOffset);
  if (SaveFlags)
    applySPAdjust(
        *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
        .findRegisterUseOperand(Z80::AF)->setIsUndef();
  BuildMI(MBB, II, DL, get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao), BaseReg)
      .addReg(BaseReg).addReg(OffsetReg, RegState::Kill)
      ->addRegisterDead(Z80::F, &TRI);
  if (SaveFlags)
    applySPAdjust(
        *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
  if (Opc == Z80::PEA24o || Opc == Z80::PEA16o) {
    MI.setDesc(get(Opc == Z80::PEA24o ? Z80::EX24sa : Z80::EX16sa));
    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, true);
    MI.getOperand(FIOperandNum + 1).ChangeToRegister(BaseReg, false);
    MI.tieOperands(0, 1);
    return;
  }
  ++II;
  if (IllegalLEA) {
    copyRegister(MBB, II, DL, MI.getOperand(0).getReg(), BaseReg);
    MI.eraseFromParent();
  } else {
    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, false);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(0);
  }
  applySPAdjust(
      *BuildMI(MBB, II, DL, get(Is24Bit ? Z80::POP24r : Z80::POP16r), BaseReg));
}

static bool isFrameStoreOpcode(int Opcode) {
  switch (Opcode) {
  default:
    return false;
  case Z80::LD8or:
  case Z80::LD8og:
  case Z80::LD16or:
  case Z80::LD88or:
  case Z80::LD24or:
    return true;
  }
}
unsigned Z80InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  if (isFrameStoreOpcode(MI.getOpcode()) && !MI.getOperand(2).getSubReg() &&
      isFrameOperand(MI, 0, FrameIndex))
    return MI.getOperand(2).getReg();
  return 0;
}

bool Z80InstrInfo::isReallyTriviallyReMaterializable(const MachineInstr &MI,
                                                     AAResults *AA) const {
  switch (MI.getOpcode()) {
  case Z80::LD8r0:
  case Z80::LD24r0:
  case Z80::LD24r_1:
    return true;
  }
  return false;
}

void Z80InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I, Register DstReg,
                                 unsigned SubIdx, const MachineInstr &Orig,
                                 const TargetRegisterInfo &TRI) const {
  if (!Orig.modifiesRegister(Z80::F, &TRI) ||
      MBB.computeRegisterLiveness(&TRI, Z80::F, I) ==
      MachineBasicBlock::LQR_Dead)
    return TargetInstrInfo::reMaterialize(MBB, I, DstReg, SubIdx, Orig, TRI);
  // The instruction clobbers F. Re-materialize as LDri to avoid side effects.
  unsigned Opc;
  int Value;
  switch (Orig.getOpcode()) {
  default: llvm_unreachable("Unexpected instruction!");
  case Z80::LD8r0:   Opc = Z80::LD8ri;  Value =  0; break;
  case Z80::LD24r0:  Opc = Z80::LD24ri; Value =  0; break;
  case Z80::LD24r_1: Opc = Z80::LD24ri; Value = -1; break;
  }
  const MachineOperand &OrigReg = Orig.getOperand(0);
  MachineInstr &NewMI =
      *BuildMI(MBB, I, Orig.getDebugLoc(), get(Opc)).add(OrigReg).addImm(Value);
  NewMI.substituteRegister(OrigReg.getReg(), DstReg, SubIdx, TRI);
}

void Z80InstrInfo::
expandLoadStoreWord(const TargetRegisterClass *ARC, unsigned AOpc,
                    const TargetRegisterClass *ORC, unsigned OOpc,
                    MachineInstr &MI, unsigned RegIdx) const {
  MCRegister Reg = MI.getOperand(RegIdx).getReg();
  assert(ARC->contains(Reg) != ORC->contains(Reg) &&
         "RegClasses should be covering and disjoint");
  MI.setDesc(get(ARC->contains(Reg) ? AOpc : OOpc));
  (void)ORC;
}

bool Z80InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  auto Next = ++MachineBasicBlock::iterator(MI);
  MachineInstrBuilder MIB(MF, MI);
  const TargetRegisterInfo &TRI = getRegisterInfo();
  bool Is24Bit = Subtarget.is24Bit();
  bool UseLEA = Is24Bit && !MF.getFunction().hasOptSize();
  LLVM_DEBUG(dbgs() << "\nZ80InstrInfo::expandPostRAPseudo:"; MI.dump());
  switch (unsigned Opc = MI.getOpcode()) {
  default:
    return false;
  case Z80::RCF:
    MI.setDesc(get(Z80::OR8ar));
    MIB.addReg(Z80::A, RegState::Undef)
        .addReg(Z80::A, RegState::ImplicitDefine);
    break;
  case Z80::LD8r0:
    if (MI.getOperand(0).getReg() == Z80::A) {
      MI.setDesc(get(Z80::XOR8ar));
      MI.getOperand(0).setIsUse();
      MI.getOperand(0).setIsUndef();
      MIB.addReg(Z80::A, RegState::ImplicitDefine);
    } else {
      MI.setDesc(get(Z80::LD8ri));
      MI.findRegisterDefOperand(Z80::F)->ChangeToImmediate(0);
    }
    break;
  case Z80::LD24r0:
  case Z80::LD24r_1:
    if (MI.getOperand(0).getReg() == Z80::UHL) {
      if (Opc == Z80::LD24r0)
        expandPostRAPseudo(*BuildMI(MBB, MI, DL, get(Z80::RCF)));
      else
        BuildMI(MBB, MI, DL, get(Z80::SCF));
      MI.setDesc(get(Z80::SBC24aa));
      MI.getOperand(0).setImplicit();
      MIB.addReg(Z80::UHL, RegState::Implicit | RegState::Undef)
          .addReg(Z80::F, RegState::Implicit);
    } else {
      MI.setDesc(get(Z80::LD24ri));
      MI.findRegisterDefOperand(Z80::F)
        ->ChangeToImmediate(Opc == Z80::LD24r0 ? 0 : -1);
    }
    break;
  case Z80::Cmp16ao:
  case Z80::Cmp24ao: {
    MCRegister Reg = Opc == Z80::Cmp24ao ? Z80::UHL : Z80::HL;
    if (MBB.computeRegisterLiveness(&TRI, Reg, Next) !=
        MachineBasicBlock::LQR_Dead) {
      BuildMI(MBB, Next, DL,
              get(Opc == Z80::Cmp24ao ? Z80::ADD24ao : Z80::ADD16ao), Reg)
          .addReg(Reg).add(MI.getOperand(0));
      MI.getOperand(0).setIsKill(false);
    }
    MIB.addReg(Reg, RegState::ImplicitDefine);
    LLVM_FALLTHROUGH;
  }
  case Z80::Sub16ao:
  case Z80::Sub24ao:
    expandPostRAPseudo(*BuildMI(MBB, MI, DL, get(Z80::RCF)));
    MI.setDesc(get(Opc == Z80::Cmp24ao || Opc == Z80::Sub24ao ? Z80::SBC24ao
                                                              : Z80::SBC16ao));
    MIB.addReg(Z80::F, RegState::Implicit);
    break;
  case Z80::Cmp16a0:
  case Z80::Cmp24a0: {
    MCRegister Reg = Opc == Z80::Cmp24a0 ? Z80::UHL : Z80::HL;
    MCRegister UndefReg = Opc == Z80::Cmp24a0 ? Z80::UBC : Z80::BC;
    BuildMI(MBB, MI, DL, get(Opc == Z80::Cmp24a0 ? Z80::ADD24ao : Z80::ADD16ao),
            Reg).addReg(Reg).addReg(UndefReg, RegState::Undef);
    expandPostRAPseudo(*BuildMI(MBB, MI, DL, get(Z80::RCF)));
    MI.setDesc(get(Opc == Z80::Cmp24a0 ? Z80::SBC24ao : Z80::SBC16ao));
    MIB.addReg(UndefReg, RegState::Undef).addReg(Reg, RegState::ImplicitDefine)
        .addReg(Z80::F, RegState::Implicit);
    break;
  }
  case Z80::LD8ro:
  case Z80::LD8rp: {
    MachineOperand &DstOp = MI.getOperand(0);
    if (Z80::I8RegClass.contains(DstOp.getReg())) {
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
          .findRegisterUseOperand(Z80::AF)->setIsUndef();
      copyPhysReg(MBB, Next, DL, DstOp.getReg(), Z80::A, true);
      DstOp.setReg(Z80::A);
      applySPAdjust(
          *BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    }
    MI.setDesc(get(Opc == Z80::LD8ro ? Z80::LD8go : Z80::LD8gp));
    break;
  }
  case Z80::LD88rp:
  case Z80::LD88ro: {
    assert(!Subtarget.has16BitEZ80Ops() &&
           "LD88rp/LD88ro should not be used on the ez80 in 16-bit mode");
    MachineOperand &DstOp = MI.getOperand(0);
    MCRegister OrigReg = DstOp.getReg();
    MCRegister Reg = OrigReg;
    const MachineOperand &AddrOp = MI.getOperand(1);
    MCRegister AddrReg = AddrOp.getReg();
    unsigned LowOpc = Opc == Z80::LD88rp ? Z80::LD8rp : Z80::LD8ro;
    unsigned HighOpc =
        RI.isSubRegisterEq(Z80::UHL, AddrReg) ? Z80::LD8rp : Z80::LD8ro;
    bool Index = Z80::I16RegClass.contains(Reg);
    if (Index)
      Reg = Z80::HL;
    bool Overlap = RI.isSubRegisterEq(AddrReg, Reg);
    if (Overlap)
      Reg = Z80::DE;
    MCRegister ScratchReg =
        Is24Bit
            ? TRI.getMatchingSuperReg(Reg, Z80::sub_short, &Z80::R24RegClass)
            : Reg;
    if (Index || Overlap)
      // Save scratch register
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
               .addReg(ScratchReg, RegState::Undef));
    MI.setDesc(get(HighOpc));
    DstOp.setReg(TRI.getSubReg(Reg, Z80::sub_high));
    if (LowOpc != HighOpc) {
      assert(LowOpc == Z80::LD8rp && HighOpc == Z80::LD8ro &&
             "Can't offset low only");
      // Fixup if converting high to offset
      MIB.addImm(1);
    }
    MIB = BuildMI(MBB, MI, DL, get(LowOpc), TRI.getSubReg(Reg, Z80::sub_low))
              .addReg(AddrReg);
    if (HighOpc == Z80::LD8rp) {
      // Compute high address
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::INC24r : Z80::INC16r), AddrReg)
          .addReg(AddrReg);
      if (!AddrOp.isKill())
        // Restore address register
        BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::DEC24r : Z80::DEC16r),
                AddrReg).addReg(AddrReg);
    } else if (LowOpc == Z80::LD8ro) {
      // Fixup if both are offset
      MachineOperand &OffsetMO = MI.getOperand(2);
      MIB.add(OffsetMO);
      OffsetMO.setImm(OffsetMO.getImm() + 1);
      assert(isInt<8>(OffsetMO.getImm()) && "LD88 can't have maximum offset.");
    }
    if (Index) {
      if (Reg == Z80::HL)
        // Restore scratch register and prepare to set original register below
        BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa),
                ScratchReg).addReg(ScratchReg);
      else
        // Set original register directly
        copyPhysReg(MBB, Next, DL, OrigReg, Reg);
    } else if (Overlap)
      copyPhysReg(MBB, Next, DL, OrigReg, Reg, true);
    if (Index || Overlap)
      // Restore scratch register or set original register
      applySPAdjust(*BuildMI(
          MBB, Next, DL, get(Is24Bit ? Z80::POP24r : Z80::POP16r),
          Index && Reg == Z80::HL
              ? Is24Bit ? TRI.getMatchingSuperReg(OrigReg, Z80::sub_short,
                                                  &Z80::R24RegClass)
                        : OrigReg
              : ScratchReg));
    expandPostRAPseudo(*MIB);
    expandPostRAPseudo(MI);
    break;
  }
  case Z80::LD8or:
  case Z80::LD8pr: {
    MachineOperand &SrcOp = MI.getOperand(MI.getNumExplicitOperands() - 1);
    if (Z80::I8RegClass.contains(SrcOp.getReg())) {
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
          .findRegisterUseOperand(Z80::AF)->setIsUndef();
      copyPhysReg(MBB, MI, DL, Z80::A, SrcOp.getReg(), SrcOp.isKill());
      SrcOp.setReg(Z80::A);
      SrcOp.setIsKill();
      applySPAdjust(
          *BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    }
    MI.setDesc(get(Opc == Z80::LD8or ? Z80::LD8og : Z80::LD8pg));
    break;
  }
  case Z80::LD88pr:
  case Z80::LD88or: {
    assert(!Subtarget.has16BitEZ80Ops() &&
           "LD88pr/LD88or should not be used on the ez80 in 16-bit mode");
    const MachineOperand &AddrOp = MI.getOperand(0);
    MCRegister AddrReg = AddrOp.getReg();
    MachineOperand &SrcOp = MI.getOperand(MI.getNumExplicitOperands() - 1);
    MCRegister OrigReg = SrcOp.getReg();
    MCRegister Reg = OrigReg;
    unsigned LowOpc = Opc == Z80::LD88pr ? Z80::LD8pr : Z80::LD8or;
    unsigned HighOpc =
        RI.isSubRegisterEq(Z80::UHL, AddrReg) ? Z80::LD8pr : Z80::LD8or;
    bool Index = Z80::I16RegClass.contains(Reg);
    if (Index)
      Reg = Z80::HL;
    bool Overlap = RI.isSubRegisterEq(AddrReg, Reg);
    if (Overlap && Index) {
      // Just use DE instead, so there's no overlap to worry about.
      Reg = Z80::DE;
      Overlap = false;
      assert(!RI.isSubRegisterEq(AddrReg, Reg) && "Shouldn't overlap");
    }
    assert((!Overlap || HighOpc == Z80::LD8pr) &&
           "If we are overlapping, then high shouldn't be offset");
    MCRegister ScratchReg =
        Is24Bit
            ? TRI.getMatchingSuperReg(Reg, Z80::sub_short, &Z80::R24RegClass)
            : Reg;
    MCRegister OrigHighReg = TRI.getSubReg(Reg, Z80::sub_high);
    MCRegister HighReg = OrigHighReg;
    if (Index) {
      MCRegister OrigSuperReg =
          TRI.getMatchingSuperReg(OrigReg, Z80::sub_short, &Z80::R24RegClass);
      // Save scratch register or prepare to set scratch register
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24r : Z80::PUSH16r))
               .addReg(UseLEA || Reg == Z80::DE ? ScratchReg
                       : Is24Bit                ? OrigSuperReg
                                                : OrigReg));
      if (UseLEA)
        // Set scratch register
        BuildMI(MBB, MI, DL, get(Z80::LEA24ro), ScratchReg)
            .addReg(OrigSuperReg).addImm(0);
      else if (Reg == Z80::HL)
        // Save and set scratch register
        BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::EX24sa : Z80::EX16sa),
                ScratchReg).addReg(ScratchReg);
      else
        // Set scratch register directly
        copyPhysReg(MBB, MI, DL, Reg, OrigReg);
    } else if (Overlap) {
      // Save A
      applySPAdjust(
          *BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::PUSH24AF : Z80::PUSH16AF)))
          .findRegisterUseOperand(Z80::AF)->setIsUndef();
      HighReg = Z80::A;
    }
    MI.setDesc(get(HighOpc));
    SrcOp.setReg(TRI.getSubReg(Reg, Z80::sub_high));
    if (LowOpc != HighOpc) {
      assert(LowOpc == Z80::LD8pr && HighOpc == Z80::LD8or &&
             "Can't offset low only");
      // Fixup if converting high to offset
      SrcOp.ChangeToImmediate(1);
      MIB.addReg(HighReg);
    } else
      SrcOp.setReg(HighReg);
    MIB = BuildMI(MBB, MI, DL, get(LowOpc)).addReg(AddrReg);
    if (HighOpc == Z80::LD8pr) {
      if (Overlap) {
        assert(!Index && "Should have changed to DE above");
        // Save high reg in A so it isn't clobbered by the inc below
        copyPhysReg(MBB, MI, DL, HighReg, OrigHighReg, false);
      }
      // Compute high address
      BuildMI(MBB, MI, DL, get(Is24Bit ? Z80::INC24r : Z80::INC16r), AddrReg)
          .addReg(AddrReg);
      if (!AddrOp.isKill())
        // Restore address register
        BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::DEC24r : Z80::DEC16r),
                AddrReg).addReg(AddrReg);
    } else if (LowOpc == Z80::LD8or) {
      // Fixup if both are offset
      MachineOperand &OffsetMO = MI.getOperand(1);
      MIB.add(OffsetMO);
      OffsetMO.setImm(OffsetMO.getImm() + 1);
      assert(isInt<8>(OffsetMO.getImm()) && "LD88 can't have maximum offset.");
    }
    MIB.addReg(TRI.getSubReg(Reg, Z80::sub_low));
    if (Index)
      // Restore scratch register
      applySPAdjust(*BuildMI(
          MBB, Next, DL, get(Is24Bit ? Z80::POP24r : Z80::POP16r), ScratchReg));
    else if (Overlap)
      // Restore A
      applySPAdjust(
          *BuildMI(MBB, Next, DL, get(Is24Bit ? Z80::POP24AF : Z80::POP16AF)));
    expandPostRAPseudo(*MIB);
    expandPostRAPseudo(MI);
    break;
  }
  case Z80::LD16rm:
    expandLoadStoreWord(&Z80::A16RegClass, Z80::LD16am,
                        &Z80::O16RegClass, Z80::LD16gm, MI, 0);
    break;
  case Z80::LD24rm:
    expandLoadStoreWord(&Z80::A24RegClass, Z80::LD24am,
                        &Z80::O24RegClass, Z80::LD24gm, MI, 0);
    break;
  case Z80::LD16mr:
    expandLoadStoreWord(&Z80::A16RegClass, Z80::LD16ma,
                        &Z80::O16RegClass, Z80::LD16mg, MI, 1);
    break;
  case Z80::LD24mr:
    expandLoadStoreWord(&Z80::A24RegClass, Z80::LD24ma,
                        &Z80::O24RegClass, Z80::LD24mg, MI, 1);
    break;
  case Z80::CALL16r:
  case Z80::CALL24r: {
    const char *Symbol;
    switch (MI.getOperand(0).getReg()) {
    default: llvm_unreachable("Unexpected indcall register");
    case Z80::HL: case Z80::UHL: Symbol = "_indcallhl"; break;
    case Z80::IX: case Z80::UIX: Symbol = "_indcallix"; break;
    case Z80::IY: case Z80::UIY: Symbol = "_indcall"; break;
    }
    MI.setDesc(get(Opc == Z80::CALL24r ? Z80::CALL24 : Z80::CALL16));
    MI.getOperand(0).ChangeToES(Symbol);
    break;
  }
  case Z80::EI_RETI:
    BuildMI(MBB, MI, DL, get(Z80::EI));
    MI.setDesc(get(Is24Bit ? Z80::RETI24 : Z80::RETI16));
    break;
  case Z80::TCRETURN16:
    MI.setDesc(get(Z80::JP16));
    break;
  case Z80::TCRETURN24:
    MI.setDesc(get(Z80::JP24));
    break;
  case Z80::TCRETURN16r:
    MI.setDesc(get(Z80::JP16r));
    break;
  case Z80::TCRETURN24r:
    MI.setDesc(get(Z80::JP24r));
    break;
  case TargetOpcode::INSERT_SUBREG:
    copyPhysReg(MBB, MI, DL, MI.getOperand(0).getReg(),
                MI.getOperand(1).getReg(), MI.getOperand(1).isKill());
    copyPhysReg(
        MBB, MI, DL,
        TRI.getSubReg(MI.getOperand(0).getReg(), MI.getOperand(3).getImm()),
        MI.getOperand(2).getReg(), MI.getOperand(2).isKill());
    MI.eraseFromParent();
    return true;
  case TargetOpcode::EXTRACT_SUBREG:
    copyPhysReg(
        MBB, MI, DL, MI.getOperand(0).getReg(),
        TRI.getSubReg(MI.getOperand(1).getReg(), MI.getOperand(2).getImm()),
        MI.getOperand(1).isKill());
    MI.eraseFromParent();
    return true;
  }
  LLVM_DEBUG(MI.dump());
  return true;
}

bool Z80InstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                  Register &SrcReg2, int64_t &CmpMask,
                                  int64_t &CmpValue) const {
  switch (MI.getOpcode()) {
  default: return false;
  case Z80::OR8ar:
  case Z80::TST8ag:
    SrcReg = Z80::A;
    if (MI.getOperand(1).getReg() != SrcReg)
      return false;
    // Compare against zero.
    SrcReg2 = 0;
    CmpMask = 0;
    CmpValue = 0;
    break;
  case Z80::CP8ai:
  case Z80::SUB8ai:
    SrcReg = Z80::A;
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = MI.getOperand(0).getImm();
    break;
  case Z80::TST8ai:
    SrcReg = Z80::A;
    SrcReg2 = 0;
    CmpMask = MI.getOperand(0).getImm();
    CmpValue = 0;
    break;
  case Z80::CP8ar:
  case Z80::SUB8ar:
    SrcReg = Z80::A;
    SrcReg2 = MI.getOperand(0).getReg();
    CmpMask = 0;
    CmpValue = 0;
    break;
  case Z80::CP8ap:
  case Z80::CP8ao:
  case Z80::SUB8ap:
  case Z80::SUB8ao:
    SrcReg = Z80::A;
    SrcReg2 = 0;
    CmpMask = 0;
    CmpValue = 0;
    break;
  }
  MachineBasicBlock::const_reverse_iterator I = MI, E = MI.getParent()->rend();
  while (++I != E && I->isFullCopy())
    for (Register *Reg : {&SrcReg, &SrcReg2})
      if (Reg->isPhysical() && *Reg == I->getOperand(0).getReg())
        *Reg = I->getOperand(1).getReg();
  return true;
}

/// Check whether the first instruction, whose only purpose is to update flags,
/// can be made redundant. CP8ar is made redundant by SUB8ar if the operands are
/// the same.
/// SrcReg, SrcReg2: register operands for FlagI.
/// ImmValue: immediate for FlagI if it takes an immediate.
inline static bool isRedundantFlagInstr(MachineInstr &FI, Register SrcReg,
                                        Register SrcReg2, int ImmMask,
                                        int ImmValue, MachineInstr &OI) {
  if (ImmMask == ~0)
    return (FI.getOpcode() == Z80::CP8ai && OI.getOpcode() == Z80::SUB8ai) &&
           OI.getOperand(1).getImm() == ImmValue;
  return (FI.getOpcode() == Z80::CP8ar && OI.getOpcode() == Z80::SUB8ar) &&
         OI.getOperand(1).getReg() == SrcReg2;
}

/// Check whether the instruction sets the sign and zero flag based on its
/// result.
inline static bool isSZSettingInstr(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default: return false;
  case Z80::INC8r:   case Z80::INC8p:   case Z80::INC8o:
  case Z80::DEC8r:   case Z80::DEC8p:   case Z80::DEC8o:
  case Z80::ADD8ar:  case Z80::ADD8ai:  case Z80::ADD8ap:  case Z80::ADD8ao:
  case Z80::ADC8ar:  case Z80::ADC8ai:  case Z80::ADC8ap:  case Z80::ADC8ao:
  case Z80::SUB8ar:  case Z80::SUB8ai:  case Z80::SUB8ap:  case Z80::SUB8ao:
  case Z80::SBC8ar:  case Z80::SBC8ai:  case Z80::SBC8ap:  case Z80::SBC8ao:
  case Z80::AND8ar:  case Z80::AND8ai:  case Z80::AND8ap:  case Z80::AND8ao:
  case Z80::XOR8ar:  case Z80::XOR8ai:  case Z80::XOR8ap:  case Z80::XOR8ao:
  case Z80:: OR8ar:  case Z80:: OR8ai:  case Z80:: OR8ap:  case Z80:: OR8ao:
  case Z80::TST8ag:  case Z80::TST8ai:  case Z80::TST8ap:
  case Z80::NEG:     case Z80::Sub16ao: case Z80::Sub24ao:
  case Z80::ADD16aa: case Z80::ADD16ao: case Z80::ADD16as:
  case Z80::SBC16aa: case Z80::SBC16ao: case Z80::SBC16as:
  case Z80::ADC16aa: case Z80::ADC16ao: case Z80::ADC16as:
  case Z80::ADD24aa: case Z80::ADD24ao: case Z80::ADD24as:
  case Z80::SBC24aa: case Z80::SBC24ao: case Z80::SBC24as:
  case Z80::ADC24aa: case Z80::ADC24ao: case Z80::ADC24as:
  case Z80::RLC8g:   case Z80::RLC8p:   case Z80::RLC8o:
  case Z80::RRC8g:   case Z80::RRC8p:   case Z80::RRC8o:
  case Z80:: RL8g:   case Z80:: RL8p:   case Z80:: RL8o:
  case Z80:: RR8g:   case Z80:: RR8p:   case Z80:: RR8o:
  case Z80::SLA8g:   case Z80::SLA8p:   case Z80::SLA8o:
  case Z80::SRA8g:   case Z80::SRA8p:   case Z80::SRA8o:
  case Z80::SRL8g:   case Z80::SRL8p:   case Z80::SRL8o:
    return true;
  }
}

inline static bool isMatchingRMW(MachineInstr &LoadMI,
                                 MachineInstr &RMWMI) {
  switch (LoadMI.getOpcode()) {
  default: return false;
  case Z80::LD8rp:
  case Z80::LD8gp:
    switch (RMWMI.getOpcode()) {
    default: return false;
    case Z80::INC8p:
    case Z80::DEC8p:
    case Z80::RLC8p:
    case Z80::RRC8p:
    case Z80:: RL8p:
    case Z80:: RR8p:
    case Z80::SLA8p:
    case Z80::SRA8p:
    case Z80::SRL8p:
      return LoadMI.getOperand(1).isIdenticalTo(RMWMI.getOperand(0));
    }
  case Z80::LD8ro:
  case Z80::LD8go:
    switch (RMWMI.getOpcode()) {
    default: return false;
    case Z80::INC8o:
    case Z80::DEC8o:
    case Z80::RLC8o:
    case Z80::RRC8o:
    case Z80:: RL8o:
    case Z80:: RR8o:
    case Z80::SLA8o:
    case Z80::SRA8o:
    case Z80::SRL8o:
      return LoadMI.getOperand(1).isIdenticalTo(RMWMI.getOperand(0)) &&
             LoadMI.getOperand(2).isIdenticalTo(RMWMI.getOperand(1));
    }
  }
}

/// Check if there exists an earlier instruction that operates on the same
/// source operands and sets flags in the same way as Compare; remove Compare if
/// possible.
bool Z80InstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                                        Register SrcReg2, int64_t CmpMask,
                                        int64_t CmpValue,
                                        const MachineRegisterInfo *MRI) const {
  // If we are comparing against zero, check whether we can use MI to update F.
  bool IsCmpZero = CmpMask == ~0 && !CmpValue;

  // Check whether we can replace SUB with CP.
  unsigned CpOpc;
  switch (CmpInstr.getOpcode()) {
  default: CpOpc = 0; break;
  case Z80::SUB8ai: CpOpc = IsCmpZero ? Z80::OR8ar : Z80::CP8ai; break;
  case Z80::SUB8ar: CpOpc = Z80::CP8ar; break;
  case Z80::SUB8ap: CpOpc = Z80::CP8ap; break;
  case Z80::SUB8ao: CpOpc = Z80::CP8ao; break;
  }
  if (CpOpc) {
    int DeadDef = CmpInstr.findRegisterDefOperandIdx(Z80::A, /*isDead*/true);
    if (DeadDef == -1)
      return false;
    // There is no use of the destination register, so we replace SUB with CP.
    CmpInstr.setDesc(get(CpOpc));
    if (CpOpc == Z80::OR8ar)
      CmpInstr.getOperand(0).ChangeToRegister(Z80::A, false);
    else
      CmpInstr.removeOperand(DeadDef);
  }

  // Get the unique definition of SrcReg.
  MachineInstr *MI;
  Register LookthroughReg = SrcReg;
  do
    MI = MRI->getUniqueVRegDef(LookthroughReg);
  while (MI && MI->isFullCopy() &&
         (LookthroughReg = MI->getOperand(1).getReg()).isVirtual());
  if (!MI) return false;

  MachineBasicBlock::iterator I = CmpInstr, Def = MI;

  const TargetRegisterInfo &TRI = getRegisterInfo();
  if (Def != MI->getParent()->begin()) {
    auto PrevII = std::prev(Def);
    if (isMatchingRMW(*MI, *PrevII))
      MI = &*PrevII;
  }

  // If MI is not in the same BB as CmpInstr, do not optimize.
  if (IsCmpZero && (MI->getParent() != CmpInstr.getParent() ||
                    !isSZSettingInstr(*MI)))
    return false;

  // We are searching for an earlier instruction, which will be stored in
  // SubInstr, that can make CmpInstr redundant.
  MachineInstr *SubInstr = nullptr;

  // We iterate backwards, starting from the instruction before CmpInstr, and
  // stopping when we reach the definition of a source register or the end of
  // the BB. RI points to the instruction before CmpInstr. If the definition is
  // in this BB, RE points to it, otherwise RE is the beginning of the BB.
  MachineBasicBlock::reverse_iterator RE = CmpInstr.getParent()->rend();
  if (CmpInstr.getParent() == MI->getParent())
    RE = Def.getReverse(); // points to MI
  for (auto RI = ++I.getReverse(); RI != RE; ++RI) {
    MachineInstr &Instr = *RI;
    // Check whether CmpInstr can be made redundant by the current instruction.
    if (!IsCmpZero && isRedundantFlagInstr(CmpInstr, SrcReg, SrcReg2, CmpMask,
                                           CmpValue, Instr)) {
      SubInstr = &Instr;
      break;
    }

    // If this instruction modifies F, we can't remove CmpInstr.
    if (Instr.modifiesRegister(Z80::F, &TRI))
      return false;
  }

  // Return false if no candidates exist.
  if (!IsCmpZero && !SubInstr)
    return false;

  // Scan forward from the instruction after CmpInstr for uses of F.
  // It is safe to remove CmpInstr if F is redefined or killed.
  // If we are at the end of the BB, we need to check whether F is live-out.
  bool IsSafe = false;
  MachineBasicBlock::iterator E = CmpInstr.getParent()->end();
  for (++I; I != E; ++I) {
    const MachineInstr &Instr = *I;
    bool ModifiesFlags = Instr.modifiesRegister(Z80::F, &TRI);
    bool UsesFlags = Instr.readsRegister(Z80::F, &TRI);
    if (ModifiesFlags && !UsesFlags) {
      IsSafe = true;
      break;
    }
    if (!ModifiesFlags && !UsesFlags)
      continue;
    if (IsCmpZero) {
      Z80::CondCode OldCC;
      switch (Instr.getOpcode()) {
      default:
        OldCC = Z80::COND_INVALID;
        break;
      case Z80::JQCC:
        OldCC = Z80::CondCode(Instr.getOperand(1).getImm());
        break;
      case Z80::ADC8ar: case Z80::ADC8ai: case Z80::ADC8ap: case Z80::ADC8ao:
      case Z80::SBC8ar: case Z80::SBC8ai: case Z80::SBC8ap: case Z80::SBC8ao:
      case Z80::SBC16ao:case Z80::ADC16ao:
        OldCC = Z80::COND_C;
        break;
      }
      switch (OldCC) {
      default: return false;
      case Z80::COND_NZ: case Z80::COND_Z:
      case Z80::COND_P: case Z80::COND_M:
        break;
      }
    }
    if (ModifiesFlags || Instr.killsRegister(Z80::F, &TRI)) {
      // It is safe to remove CmpInstr if F is updated again or killed.
      IsSafe = true;
      break;
    }
  }
  if (IsCmpZero && !IsSafe) {
    MachineBasicBlock *MBB = CmpInstr.getParent();
    for (MachineBasicBlock *Successor : MBB->successors())
      if (Successor->isLiveIn(Z80::F))
        return false;
  }

  // The instruction to be updated is either Sub or MI.
  if (IsCmpZero)
    SubInstr = MI;

  // Make sure Sub instruction defines F and mark the def live.
  unsigned i = 0, e = SubInstr->getNumOperands();
  for (; i != e; ++i) {
    MachineOperand &MO = SubInstr->getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == Z80::F) {
      MO.setIsDead(false);
      break;
    }
  }
  assert(i != e && "Unable to locate a def EFLAGS operand");

  CmpInstr.eraseFromParent();
  return true;
}

MachineInstr *Z80InstrInfo::optimizeLoadInstr(MachineInstr &MI,
                                              const MachineRegisterInfo *MRI,
                                              Register &FoldAsLoadDefReg,
                                              MachineInstr *&DefMI) const {
  // Check whether we can move DefMI here.
  DefMI = MRI->getUniqueVRegDef(FoldAsLoadDefReg);
  bool SawStore = false;
  if (!DefMI || !DefMI->isSafeToMove(nullptr, SawStore))
    return nullptr;

  // Collect information about virtual register operands of MI.
  SmallVector<unsigned, 1> SrcOperandIds;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg != FoldAsLoadDefReg)
      continue;
    // Do not fold if we have a subreg use or a def.
    if (MO.getSubReg() || MO.isDef())
      return nullptr;
    SrcOperandIds.push_back(i);
  }
  if (SrcOperandIds.empty())
    return nullptr;

  // Check whether we can fold the def into SrcOperandId.
  if (MachineInstr *FoldMI = foldMemoryOperand(MI, SrcOperandIds, *DefMI)) {
    FoldAsLoadDefReg = Register();
    return FoldMI;
  }

  return nullptr;
}

bool Z80InstrInfo::isRegisterOperandSubClassEq(
    const MachineOperand &RegMO, const TargetRegisterClass &RC) const {
  assert(RegMO.isReg() && "Expected a register operand.");

  const TargetRegisterInfo &TRI = getRegisterInfo();
  const MachineInstr &MI = *RegMO.getParent();
  const MachineFunction &MF = *MI.getMF();
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  Register Reg = RegMO.getReg();
  if (Reg.isPhysical())
    return RC.contains(Reg);

  assert(Reg.isVirtual() && "Expected a physical or virtual register.");
  const TargetRegisterClass &RegRC = *MRI.getRegClass(Reg);
  if (unsigned SubReg = RegMO.getSubReg())
    return TRI.getMatchingSuperRegClass(&RegRC, &RC, SubReg) == &RegRC;
  return RC.hasSubClassEq(&RegRC);
}

void Z80InstrInfo::updateOperandRegConstraints(MachineFunction &MF,
                                               MachineInstr &NewMI) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = getRegisterInfo();

  for (unsigned Idx : llvm::seq(0u, NewMI.getNumOperands())) {
    MachineOperand &MO = NewMI.getOperand(Idx);
    // We only need to update constraints on virtual register operands.
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;

    auto *NewRC =
        MRI.constrainRegClass(Reg, getRegClass(NewMI.getDesc(), Idx, &TRI, MF));
    if (!NewRC) {
      LLVM_DEBUG(
          dbgs() << "WARNING: Unable to update register constraint for operand "
                 << Idx << " of instruction:\n";
          NewMI.dump(); dbgs() << "\n");
    }
  }
}

MachineInstr *Z80InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, unsigned OpNum,
    ArrayRef<MachineOperand> MOs, MachineBasicBlock::iterator InsertPt,
    unsigned Size) const {
  bool IsOff;
  if (MOs.size() == 1 && MOs[0].isReg())
    IsOff = false;
  else if (MOs.size() == 2 && (MOs[0].isReg() || MOs[0].isFI()) &&
           MOs[1].isImm())
    IsOff = true;
  else
    return nullptr;

  unsigned Opc;
  unsigned OpSize = 1;
  switch (OpNum) {
  default: return nullptr;
  case 0:
    switch (MI.getOpcode()) {
    default: return nullptr;
    case Z80::BIT8gb: Opc = IsOff ? Z80::BIT8ob : Z80::BIT8pb; break;
    case Z80::ADD8ar: Opc = IsOff ? Z80::ADD8ao : Z80::ADD8ap; break;
    case Z80::ADC8ar: Opc = IsOff ? Z80::ADC8ao : Z80::ADC8ap; break;
    case Z80::SUB8ar: Opc = IsOff ? Z80::SUB8ao : Z80::SUB8ap; break;
    case Z80::SBC8ar: Opc = IsOff ? Z80::SBC8ao : Z80::SBC8ap; break;
    case Z80::AND8ar: Opc = IsOff ? Z80::AND8ao : Z80::AND8ap; break;
    case Z80::XOR8ar: Opc = IsOff ? Z80::XOR8ao : Z80::XOR8ap; break;
    case Z80:: OR8ar: Opc = IsOff ? Z80:: OR8ao : Z80:: OR8ap; break;
    case Z80::TST8ag:
      if (IsOff)
        return nullptr;
      Opc = Z80::TST8ap;
      break;
    case TargetOpcode::COPY:
      if (!isRegisterOperandSubClassEq(MI.getOperand(1), Z80::R8RegClass))
        return nullptr;
      Opc = IsOff ? Z80::LD8or : Z80::LD8pr;
      break;
    }
    break;
  case 1:
    switch (MI.getOpcode()) {
    default: return nullptr;
    case TargetOpcode::COPY:
      if (!isRegisterOperandSubClassEq(MI.getOperand(0), Z80::R8RegClass))
        return nullptr;
      Opc = IsOff ? Z80::LD8ro : Z80::LD8rp;
      break;
    }
    break;
  }

  if (Size && Size != OpSize)
    return nullptr;

  MachineInstrBuilder MIB(
      MF, MF.CreateMachineInstr(get(Opc), MI.getDebugLoc(), true));
  for (unsigned Idx : llvm::seq(0u, MI.getNumOperands())) {
    MachineOperand &MO = MI.getOperand(Idx);
    if (Idx == OpNum) {
      assert(MO.isReg() && "Expected to fold into reg operand!");
      for (auto &AddrMO : MOs)
        MIB.add(AddrMO);
    } else
      MIB.add(MO);
  }
  updateOperandRegConstraints(MF, *MIB);
  InsertPt->getParent()->insert(InsertPt, MIB);
  return MIB;
}

MachineInstr *
Z80InstrInfo::foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                    ArrayRef<unsigned> Ops,
                                    MachineBasicBlock::iterator InsertPt,
                                    int FrameIndex, LiveIntervals *LIS,
                                    VirtRegMap *VRM) const {
  for (auto Op : Ops)
    if (MI.getOperand(Op).getSubReg())
      return nullptr;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Size = MFI.getObjectSize(FrameIndex);

  if (Ops.size() != 1)
    return nullptr;

  MachineOperand MOs[2] = {MachineOperand::CreateFI(FrameIndex),
                           MachineOperand::CreateImm(0)};
  return foldMemoryOperandImpl(MF, MI, Ops[0], MOs, InsertPt, Size);
}

MachineInstr *
Z80InstrInfo::foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                    ArrayRef<unsigned> Ops,
                                    MachineBasicBlock::iterator InsertPt,
                                    MachineInstr &LoadMI,
                                    LiveIntervals *LIS) const {
  for (auto Op : Ops)
    if (MI.getOperand(Op).getSubReg())
      return nullptr;

  int FrameIndex;
  if (isLoadFromStackSlot(LoadMI, FrameIndex))
    return foldMemoryOperandImpl(MF, MI, Ops, InsertPt, FrameIndex, LIS);

  if (Ops.size() != 1)
    return nullptr;

  auto MOs = LoadMI.explicit_uses();
  return foldMemoryOperandImpl(MF, MI, Ops[0], {MOs.begin(), MOs.end()},
                               InsertPt);
}
