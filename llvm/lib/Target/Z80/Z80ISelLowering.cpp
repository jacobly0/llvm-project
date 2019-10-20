//===-- Z80ISelLowering.cpp - Z80 DAG Lowering Implementation -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Z80 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "Z80ISelLowering.h"
#include "Z80TargetMachine.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
using namespace llvm;

#define DEBUG_TYPE "z80-isel"

void Z80TargetLowering::setLibcall(RTLIB::Libcall Call, const char *Name,
                                   CallingConv::ID CC) {
  setLibcallName(Call, Name);
  setLibcallCallingConv(Call, CC);
}

Z80TargetLowering::Z80TargetLowering(const Z80TargetMachine &TM,
                                     const Z80Subtarget &STI)
    : TargetLowering(TM), Subtarget(STI) {
  bool HasEZ80Ops = Subtarget.hasEZ80Ops(), Is24Bit = Subtarget.is24Bit();
  MVT PtrVT = MVT::getIntegerVT(TM.getPointerSizeInBits(0));

  setSchedulingPreference(Sched::RegPressure);

  // Set up the register classes.
  addRegisterClass(MVT::i8, &Z80::R8RegClass);
  addRegisterClass(MVT::i16, &Z80::R16RegClass);
  if (Is24Bit)
    addRegisterClass(MVT::i24, &Z80::R24RegClass);

  setStackPointerRegisterToSaveRestore(Is24Bit ? Z80::SPL : Z80::SPS);

  // Compute derived properties from the register classes
  computeRegisterProperties(STI.getRegisterInfo());

  setLibcall(RTLIB::ZEXT_I16_I24,     "_stoiu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SEXT_I16_I24,     "_stoi",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SEXT_I24_I32,     "_itol",     CallingConv::Z80_LibCall   );

  setLibcall(RTLIB::NOT_I16,          "_snot",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NOT_I24,          "_inot",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NOT_I32,          "_lnot",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NOT_I64,          "_llnot",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::AND_I16,          "_sand",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::AND_I24,          "_iand",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::AND_I32,          "_land",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::AND_I64,          "_lland",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::OR_I16,           "_sor",      CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::OR_I24,           "_ior",      CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::OR_I32,           "_lor",      CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::OR_I64,           "_llor",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::XOR_I16,          "_sxor",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::XOR_I24,          "_ixor",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::XOR_I32,          "_lxor",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::XOR_I64,          "_llxor",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SHL_I8,           "_bshl",     CallingConv::Z80_LibCall_AB);
  setLibcall(RTLIB::SHL_I16,          "_sshl",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SHL_I16_I8,       "_sshl_b",   CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SHL_I24,          "_ishl",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SHL_I24_I8,       "_ishl_b",   CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SHL_I32,          "_lshl",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::SHL_I64,          "_llshl",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRA_I8,           "_bshrs",    CallingConv::Z80_LibCall_AB);
  setLibcall(RTLIB::SRA_I16,          "_sshrs",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRA_I16_I8,       "_sshrs_b",  CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SRA_I24,          "_ishrs",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRA_I24_I8,       "_ishrs_b",  CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SRA_I32,          "_lshrs",    CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::SRA_I64,          "_llshrs",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRL_I8,           "_bshru",    CallingConv::Z80_LibCall_AB);
  setLibcall(RTLIB::SRL_I16,          "_sshru",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRL_I16_I8,       "_sshru_b",  CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SRL_I24,          "_ishru",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SRL_I24_I8,       "_ishru_b",  CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SRL_I32,          "_lshru",    CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::SRL_I64,          "_llshru",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::CMP_I32,          "_lcmpu",    CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::CMP_I64,          "_llcmpu",   CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::CMP_I16_0,        "_scmpzero", CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::CMP_I24_0,        "_icmpzero", CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::CMP_I32_0,        "_lcmpzero", CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::CMP_I64_0,        "_llcmpzero",CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::SCMP,             "_setflag",  CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::NEG_I16,          "_sneg",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NEG_I24,          "_ineg",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NEG_I32,          "_lneg",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NEG_I64,          "_llneg",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::ADD_I32,          "_ladd",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::ADD_I32_I8,       "_ladd_b",   CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::ADD_I64,          "_lladd",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SUB_I32,          "_lsub",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SUB_I64,          "_llsub",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::MUL_I8,           "_bmulu",    CallingConv::Z80_LibCall_BC);
  setLibcall(RTLIB::MUL_I16,          "_smulu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::MUL_I24,          "_imulu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::MUL_I24_I8,       "_imul_b",   CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::MUL_I32,          "_lmulu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::MUL_I64,          "_llmulu",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SDIV_I8,          "_bdivs",    CallingConv::Z80_LibCall_BC);
  setLibcall(RTLIB::SDIV_I16,         "_sdivs",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SDIV_I24,         "_idivs",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SDIV_I32,         "_ldivs",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SDIV_I64,         "_lldivs",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIV_I8,          "_bdivu",    CallingConv::Z80_LibCall_BC);
  setLibcall(RTLIB::UDIV_I16,         "_sdivu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIV_I24,         "_idivu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIV_I32,         "_ldivu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIV_I64,         "_lldivu",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SREM_I8,          "_brems",    CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::SREM_I16,         "_srems",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SREM_I24,         "_irems",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SREM_I32,         "_lrems",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SREM_I64,         "_llrems",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UREM_I8,          "_bremu",    CallingConv::Z80_LibCall_AC);
  setLibcall(RTLIB::UREM_I16,         "_sremu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UREM_I24,         "_iremu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UREM_I32,         "_lremu",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UREM_I64,         "_llremu",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIVREM_I24,      "_idvrmu",   CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UDIVREM_I32,      "_ldvrmu",   CallingConv::Z80_LibCall   );

  setLibcall(RTLIB::POPCNT_I8,        "_bpopcnt",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::POPCNT_I16,       "_spopcnt",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::POPCNT_I24,       "_ipopcnt",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::POPCNT_I32,       "_lpopcnt",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::POPCNT_I64,       "_llpopcnt", CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::BITREV_I8,        "_bbitrev",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::BITREV_I16,       "_sbitrev",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::BITREV_I24,       "_ibitrev",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::BITREV_I32,       "_lbitrev",  CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::BITREV_I64,       "_llbitrev", CallingConv::Z80_LibCall   );

  setLibcall(RTLIB::ADD_F32,          "_fadd",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::SUB_F32,          "_fsub",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::MUL_F32,          "_fmul",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::DIV_F32,          "_fdiv",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::REM_F32,          "_frem",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::NEG_F32,          "_fneg",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::CMP_F32,          "_fcmp",     CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::FPTOSINT_F32_I32, "_ftol",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::FPTOUINT_F32_I32, "_ftol",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::SINTTOFP_I32_F32, "_ltof",     CallingConv::Z80_LibCall_L );
  setLibcall(RTLIB::UINTTOFP_I32_F32, "_ultof",    CallingConv::Z80_LibCall_L );

  setLibcall(RTLIB::ADD_F64,          "_dadd",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SUB_F64,          "_dsub",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::MUL_F64,          "_dmul",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::DIV_F64,          "_ddiv",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::REM_F64,          "_drem",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::NEG_F64,          "_dneg",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::CMP_F64,          "_dcmp",     CallingConv::Z80_LibCall_F );
  setLibcall(RTLIB::FPEXT_F32_F64,    "_ftod",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::FPROUND_F64_F32,  "_dtof",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::FPTOSINT_F64_I32, "_dtol",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::FPTOUINT_F64_I32, "_dtoul",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SINTTOFP_I32_F64, "_ltod",     CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UINTTOFP_I32_F64, "_ultod",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::FPTOSINT_F64_I64, "_dtoll",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::FPTOUINT_F64_I64, "_dtoll",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::SINTTOFP_I64_F64, "_lltod",    CallingConv::Z80_LibCall   );
  setLibcall(RTLIB::UINTTOFP_I64_F64, "_ulltod",   CallingConv::Z80_LibCall   );
}

unsigned Z80TargetLowering::getJumpTableEncoding() const {
  return MachineJumpTableInfo::EK_BlockAddress;
}

/// Return true if the target has native support for the specified value type
/// and it is 'desirable' to use the type for the given node type. e.g. On ez80
/// i16 is legal, but undesirable since i16 instruction encodings are longer and
/// slower.
bool Z80TargetLowering::isTypeDesirableForOp(unsigned Opc, EVT VT) const {
  if (!isTypeLegal(VT))
    return false;
  if (Subtarget.is16Bit())
    return true;

  switch (Opc) {
  default:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return true;
  case ISD::LOAD:
  case ISD::STORE:
  case ISD::ADD:
  case ISD::SUB:
    return VT != MVT::i16;
  case ISD::MUL:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    return VT != MVT::i24;
  }
}

/// Return true if x op y -> (SrcVT)((DstVT)x op (DstVT)y) is beneficial.
bool Z80TargetLowering::isDesirableToShrinkOp(unsigned Opc, EVT SrcVT,
                                              EVT DstVT) const {
  if (!isTypeLegal(DstVT))
    return false;
  switch (Opc) {
  default:
    return false;
  case ISD::ADD:
  case ISD::SUB:
  case ISD::ADDC:
  case ISD::SUBC:
  case ISD::ADDE:
  case ISD::SUBE:
    // These require a .sis suffix for i24 -> i16
    return DstVT != MVT::i16 || Subtarget.is16Bit();
  case ISD::MUL:
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::ROTL:
  case ISD::ROTR:
    // These are more expensive on larger types, so always shrink.
    return true;
  }
}

/// This method query the target whether it is beneficial for dag combiner to
/// promote the specified node. If true, it should return the desired promotion
/// type by reference.
bool Z80TargetLowering::IsDesirableToPromoteOp(SDValue Op, EVT &PVT) const {
  if (isDesirableToShrinkOp(Op.getOpcode(), MVT::i24, Op.getValueType()))
      return false;
  PVT = MVT::i24;
  return true;
}

MachineBasicBlock *
Z80TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  switch (MI.getOpcode()) {
  default: llvm_unreachable("Unexpected instr type to insert");
  /*case Z80::Sub016:
  case Z80::Sub024:
    return EmitLoweredSub0(MI, BB);
  case Z80::Sub16:
  case Z80::Sub24:
    return EmitLoweredSub(MI, BB);
  case Z80::Cp16a0:
  case Z80::Cp24a0:
    return EmitLoweredCmp0(MI, BB);
  case Z80::Cp16ao:
  case Z80::Cp24ao:
  return EmitLoweredCmp(MI, BB);*/
  case Z80::Select8:
  case Z80::Select16:
  case Z80::Select24:
    return EmitLoweredSelect(MI, BB);
  case Z80::SExt8:
  case Z80::SExt16:
  case Z80::SExt24:
    return EmitLoweredSExt(MI, BB);
  case Z80::LDR16:
  case Z80::LDR24:
    return EmitLoweredMemMove(MI, BB);
  }
}

void Z80TargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                      SDNode *Node) const {
  switch (MI.getOpcode()) {
  default: llvm_unreachable("Unexpected instr type to insert");
  case Z80::ADJCALLSTACKUP16:
  case Z80::ADJCALLSTACKUP24:
  case Z80::ADJCALLSTACKDOWN16:
  case Z80::ADJCALLSTACKDOWN24:
    return AdjustAdjCallStack(MI);
  }
}

void Z80TargetLowering::AdjustAdjCallStack(MachineInstr &MI) const {
  bool Is24Bit = MI.getOpcode() == Z80::ADJCALLSTACKUP24 ||
                 MI.getOpcode() == Z80::ADJCALLSTACKDOWN24;
  assert((Is24Bit || MI.getOpcode() == Z80::ADJCALLSTACKUP16 ||
                     MI.getOpcode() == Z80::ADJCALLSTACKDOWN16) &&
         "Unexpected opcode");
  MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  unsigned Reg = MRI.createVirtualRegister(Is24Bit ? &Z80::A24RegClass
                                                   : &Z80::A16RegClass);
  MachineInstrBuilder(*MI.getParent()->getParent(), MI)
    .addReg(Reg, RegState::ImplicitDefine | RegState::Dead);
  LLVM_DEBUG(MI.dump());
}

MachineBasicBlock *
Z80TargetLowering::EmitLoweredSub(MachineInstr &MI,
                                  MachineBasicBlock *BB) const {
  bool Is24Bit = MI.getOpcode() == Z80::SUB24ao;
  assert((Is24Bit || MI.getOpcode() == Z80::SUB16ao) && "Unexpected opcode");
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  LLVM_DEBUG(BB->dump());
  BuildMI(*BB, MI, DL, TII->get(Z80::RCF));
  BuildMI(*BB, MI, DL, TII->get(Is24Bit ? Z80::SBC24ao : Z80::SBC16ao))
          .addReg(MI.getOperand(0).getReg());
  MI.eraseFromParent();
  LLVM_DEBUG(BB->dump());
  return BB;
}

MachineBasicBlock *
Z80TargetLowering::EmitLoweredCmp(MachineInstr &MI,
                                  MachineBasicBlock *BB) const {
  bool Is24Bit = MI.getOpcode() == Z80::CP24ao;
  assert((Is24Bit || MI.getOpcode() == Z80::CP16ao) && "Unexpected opcode");
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  BuildMI(*BB, MI, DL, TII->get(Z80::RCF));
  BuildMI(*BB, MI, DL, TII->get(Is24Bit ? Z80::SBC24ao : Z80::SBC16ao))
    .addReg(MI.getOperand(0).getReg());
  BuildMI(*BB, MI, DL, TII->get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao),
          Is24Bit ? Z80::UHL : Z80::HL).addReg(Is24Bit ? Z80::UHL : Z80::HL)
    .addReg(MI.getOperand(0).getReg());
  MI.eraseFromParent();
  LLVM_DEBUG(BB->dump());
  return BB;
}

MachineBasicBlock *
Z80TargetLowering::EmitLoweredCmp0(MachineInstr &MI,
                                   MachineBasicBlock *BB) const {
  bool Is24Bit = MI.getOpcode() == Z80::CP24a0;
  assert((Is24Bit || MI.getOpcode() == Z80::CP16a0) && "Unexpected opcode");
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  BuildMI(*BB, MI, DL, TII->get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao),
          Is24Bit ? Z80::UHL : Z80::HL).addReg(Is24Bit ? Z80::UHL : Z80::HL)
    .addReg(MI.getOperand(0).getReg());
  BuildMI(*BB, MI, DL, TII->get(Z80::RCF));
  BuildMI(*BB, MI, DL, TII->get(Is24Bit ? Z80::SBC24ao : Z80::SBC16ao))
    .addReg(MI.getOperand(0).getReg());
  MI.eraseFromParent();
  LLVM_DEBUG(BB->dump());
  return BB;
}

MachineBasicBlock *
Z80TargetLowering::EmitLoweredSelect(MachineInstr &MI,
                                     MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // To "insert" a SELECT_CC instruction, we actually have to insert the
  // diamond control-flow pattern.  The incoming instruction knows the
  // destination vreg to set, the condition code register to branch on, the
  // true/false values to select between, and a branch opcode to use.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  //  thisMBB:
  //  ...
  //   %FalseVal = ...
  //   cmpTY ccX, r1, r2
  //   bCC copy1MBB
  //   fallthrough --> copy0MBB
  MachineBasicBlock *thisMBB = BB;
  MachineFunction *F = BB->getParent();
  MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *copy1MBB = F->CreateMachineBasicBlock(LLVM_BB);
  F->insert(I, copy0MBB);
  F->insert(I, copy1MBB);

  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block which will contain the Phi node for the select.
  copy1MBB->splice(copy1MBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
  copy1MBB->transferSuccessorsAndUpdatePHIs(BB);
  // Next, add the true and fallthrough blocks as its successors.
  BB->addSuccessor(copy0MBB);
  BB->addSuccessor(copy1MBB);

  BuildMI(BB, DL, TII->get(Z80::JQCC)).addMBB(copy1MBB)
    .addImm(MI.getOperand(3).getImm());

  //  copy0MBB:
  //   %TrueVal = ...
  //   # fallthrough to copy1MBB
  BB = copy0MBB;

  // Update machine-CFG edges
  BB->addSuccessor(copy1MBB);

  //  copy1MBB:
  //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
  //  ...
  BB = copy1MBB;
  BuildMI(*BB, BB->begin(), DL, TII->get(Z80::PHI),
          MI.getOperand(0).getReg())
    .addReg(MI.getOperand(1).getReg()).addMBB(thisMBB)
    .addReg(MI.getOperand(2).getReg()).addMBB(copy0MBB);

  MI.eraseFromParent();   // The pseudo instruction is gone now.
  LLVM_DEBUG(F->dump());
  return BB;
}

MachineBasicBlock *Z80TargetLowering::EmitLoweredSExt(
    MachineInstr &MI, MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();
  unsigned Opc, Reg;
  switch (MI.getOpcode()) {
  default: llvm_unreachable("Unexpected opcode");
  case Z80::SExt8:
    Opc = Z80::SBC8ar;
    Reg = Z80::A;
    break;
  case Z80::SExt16:
    Opc = Z80::SBC16aa;
    Reg = Z80::HL;
    break;
  case Z80::SExt24:
    Opc = Z80::SBC24aa;
    Reg = Z80::UHL;
    break;
  }
  MachineInstrBuilder MIB = BuildMI(*BB, MI, DL, TII->get(Opc));
  MIB->findRegisterUseOperand(Reg)->setIsUndef();
  if (Reg == Z80::A)
    MIB.addReg(Reg, RegState::Undef);
  MI.eraseFromParent();
  return BB;
}

MachineBasicBlock *
Z80TargetLowering::EmitLoweredMemMove(MachineInstr &MI,
                                      MachineBasicBlock *BB) const {
  bool Is24Bit = MI.getOpcode() == Z80::LDR24;
  Register DE = Is24Bit ? Z80::UDE : Z80::DE;
  Register HL = Is24Bit ? Z80::UHL : Z80::HL;
  Register BC = Is24Bit ? Z80::UBC : Z80::BC;
  Register PhysRegs[] = { DE, HL, BC };
  assert((Is24Bit || MI.getOpcode() == Z80::LDR16) && "Unexpected opcode");

  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // A memmove needs to choose between a forward and backwards copy.
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator I = ++BB->getIterator();

  MachineFunction *F = BB->getParent();
  MachineBasicBlock *LDIR_BB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *LDDR_BB = F->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *NextBB = F->CreateMachineBasicBlock(LLVM_BB);
  for (auto *MBB : {LDIR_BB, LDDR_BB}) {
    F->insert(I, MBB);
    for (auto LiveIn : {BC, DE, HL})
      MBB->addLiveIn(LiveIn);
  }
  F->insert(I, NextBB);

  // Update machine-CFG edges by transferring all successors of the current
  // block to the new block.
  NextBB->splice(NextBB->begin(), BB,
                 std::next(MachineBasicBlock::iterator(MI)), BB->end());
  NextBB->transferSuccessorsAndUpdatePHIs(BB);

  // BB:
  //   JP C,LDDR_BB
  //   fallthrough --> LDIR_BB
  BuildMI(BB, DL, TII->get(Z80::JQCC)).addMBB(LDDR_BB)
      .addImm(Z80::COND_C);
  // Next, add the LDIR and LDDR blocks as its successors.
  BB->addSuccessor(LDIR_BB);
  BB->addSuccessor(LDDR_BB);

  // LDIR_BB:
  //   LDIR
  //   JP NextBB
  for (int I = 0; I != 3; ++I)
    BuildMI(LDIR_BB, DL, TII->get(Z80::COPY), PhysRegs[I])
        .add(MI.getOperand(I));
  BuildMI(LDIR_BB, DL, TII->get(Is24Bit ? Z80::LDIR24 : Z80::LDIR16));
  BuildMI(LDIR_BB, DL, TII->get(Z80::JQ)).addMBB(NextBB);
  // Update machine-CFG edges
  LDIR_BB->addSuccessor(NextBB);

  // LDDR_BB:
  //   ADD HL,BC
  //   DEC HL
  //   EX DE,HL
  //   ADD HL,BC
  //   DEC HL
  //   EX DE,HL
  //   LDDR
  // # Fallthrough to Next MBB
  for (int I = 0; I != 3; ++I)
    BuildMI(LDDR_BB, DL, TII->get(Z80::COPY), PhysRegs[I])
        .add(MI.getOperand(I));
  for (int I = 0; I != 2; ++I) {
    BuildMI(LDDR_BB, DL, TII->get(Is24Bit ? Z80::ADD24ao : Z80::ADD16ao), HL)
        .addUse(HL).addUse(BC);
    BuildMI(LDDR_BB, DL, TII->get(Is24Bit ? Z80::DEC24r : Z80::DEC16r), HL)
        .addUse(HL);
    BuildMI(LDDR_BB, DL, TII->get(Is24Bit ? Z80::EX24DE : Z80::EX16DE));
  }
  BuildMI(LDDR_BB, DL, TII->get(Is24Bit ? Z80::LDDR24 : Z80::LDDR16));
  LDDR_BB->addSuccessor(NextBB);

  MI.eraseFromParent();   // The pseudo instruction is gone now.
  LLVM_DEBUG(F->dump());
  return NextBB;
}

/// HandleByVal - Target-specific cleanup for ByVal support.
void Z80TargetLowering::HandleByVal(CCState *State, unsigned &Size, unsigned Align) const {
  // Round up to a multiple of the stack slot size.
  Size = alignTo(Size, Subtarget.is24Bit() ? 3 : 2);
}
