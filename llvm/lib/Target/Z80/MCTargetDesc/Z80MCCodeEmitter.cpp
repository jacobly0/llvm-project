//===-- Z80MCCodeEmitter.cpp - Convert Z80 Code to Machine Code -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Z80MCCodeEmitter class.
//
//===----------------------------------------------------------------------===//

#include "Z80MCCodeEmitter.h"

#include "MCTargetDesc/Z80MCTargetDesc.h"

#include "Z80FixupKinds.h"
#include "Z80InstrInfo.h"
#include "Z80RegisterInfo.h"
#include "Z80Subtarget.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <string>

#define DEBUG_TYPE "mccodeemitter"

#define GET_INSTRMAP_INFO
#include "Z80GenInstrInfo.inc"
#undef GET_INSTRMAP_INFO

namespace llvm {

static void check_num_operands(StringRef opc, unsigned expct, unsigned actual) {
  if (expct != actual) {
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "Invalid number of arguments for instruction " << opc << ": ";
    Msg << expct << " vs " << actual << '.';
    report_fatal_error(Msg.str());
  }
}

static LLVM_ATTRIBUTE_NORETURN void report_fatal_instr_problem(StringRef opc,
                                                               const char *er) {
  std::string msg;
  raw_string_ostream Msg(msg);
  Msg << opc << ": " << er;
  report_fatal_error(Msg.str());
}

void Z80MCCodeEmitter::emitByte(uint8_t C, unsigned &CurByte,
                                raw_ostream &OS) const {
  OS << (static_cast<char>(C));
  ++CurByte;
}

void Z80MCCodeEmitter::encodeInstruction(const MCInst &MI, raw_ostream &OS,
                                         SmallVectorImpl<MCFixup> &Fixups,
                                         const MCSubtargetInfo &STI) const {
  // Keep track of the current byte being emitted.
  unsigned CurByte = 0U;

  const unsigned opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(opcode);
  const unsigned numOperands = Desc.getNumOperands();
  const MCInst::const_iterator Operands = MI.begin();

  if (Z80II::EZ80Mode == ((Desc.TSFlags) & Z80II::ModeMask)) {
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "EZ80 machine instructions not supported (yet?)";
    report_fatal_error(Msg.str());
  }
  if (Desc.isPseudo()) {
    switch (opcode) {
    case Z80::JQ:
      check_num_operands(MCII.getName(opcode), 1U, numOperands);
      if (!(Operands[0].isExpr()))
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Operand should be an expression.");
#ifdef EMIT_JR_INSTEAD_OF_JP
      emitByte(0x18U, CurByte, OS);
      Fixups.push_back(MCFixup::create(
          CurByte, Operands[0].getExpr(),
          static_cast<MCFixupKind>(Z80::fixup_8_pcrel), MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
#else
      emitByte(0xc3U, CurByte, OS);
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
#endif
      break;
    case Z80::JQCC:
      check_num_operands(MCII.getName(opcode), 2U, numOperands);
      if (!(Operands[0].isExpr()))
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "First operand should be an expression.");
      if (!(Operands[1].isImm()))
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Second operand should be immediate.");
#ifdef EMIT_JRCC_INSTEAD_OF_JPCC
      if (4U <= static_cast<uint8_t>(Operands[1].getImm()))
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Second operand should be in range 0..3.");
#else
      if (8U <= static_cast<uint8_t>(Operands[1].getImm()))
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Second operand should be in range 0..7.");
#endif
#ifdef EMIT_JRCC_INSTEAD_OF_JPCC
      emitByte((static_cast<uint8_t>(Operands[1].getImm()) << 3U) | 0x20U,
               CurByte, OS);
      Fixups.push_back(MCFixup::create(
          CurByte, Operands[0].getExpr(),
          static_cast<MCFixupKind>(Z80::fixup_8_pcrel), MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
#else
      emitByte((static_cast<uint8_t>(Operands[1].getImm()) << 3U) | 0xc2U,
               CurByte, OS);
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
#endif
      break;
    default:
      std::string msg;
      raw_string_ostream Msg(msg);
      Msg << "Not supported pseudo instr: " << MI;
      report_fatal_error(Msg.str());
    }
    return;
  }
  switch (opcode) {
  case Z80::ADC8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xceU, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::ADC8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x8eU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::ADC8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0x8eU, CurByte, OS);
    break;
  case Z80::ADC8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x8fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x88U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x89U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x8aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x8bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x8cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x8dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x8cU, CurByte, OS); // ADC A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x8dU, CurByte, OS); // ADC A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x8cU, CurByte, OS); // ADC A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x8dU, CurByte, OS); // ADC A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::ADD16SP:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    if ((Operands[0].getReg()) != (Operands[1].getReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be the same register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0x39U, CurByte, OS);
    break;
  case Z80::ADD16aa:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    if ((Operands[0].getReg()) != (Operands[1].getReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be the same register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0x29U, CurByte, OS);
    break;
  case Z80::ADD16ao:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg()) &&
          (Operands[2].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "All operands should be registers.");
    if ((Operands[0].getReg()) != (Operands[1].getReg()))
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "First two of the operands should be the same register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed first two registers are HL, IX, IY.");
    }
    switch (Operands[2].getReg()) {
    case Z80::BC:
      emitByte(0x09U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0x19U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed last registers are BC, DE.");
    }
    break;
  case Z80::ADD8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xc6U, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::ADD8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x86U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::ADD8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0x86U, CurByte, OS);
    break;
  case Z80::ADD8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x87U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x80U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x81U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x82U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x83U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x84U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x85U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x84U, CurByte, OS); // ADD A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x85U, CurByte, OS); // ADD A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x84U, CurByte, OS); // ADD A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x85U, CurByte, OS); // ADD A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::AND8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xe6U, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::AND8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xa6U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::AND8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xa6U, CurByte, OS);
    break;
  case Z80::AND8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xa7U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xa0U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xa1U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xa2U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xa3U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xa4U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xa5U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xa4U, CurByte, OS); // AND A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xa5U, CurByte, OS); // AND A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xa4U, CurByte, OS); // AND A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xa5U, CurByte, OS); // AND A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::BIT8bg:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    switch (Operands[1].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x47U,
               CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x40U,
               CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x41U,
               CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x42U,
               CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x43U,
               CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x44U,
               CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x45U,
               CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x44U,
               CurByte, OS);        // BIT b, H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x45U,
               CurByte, OS);        // BIT b, L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x44U,
               CurByte, OS);        // BIT b, H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x45U,
               CurByte, OS);        // BIT b, L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::BIT8bo:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    if (!(Operands[2].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Third operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    switch (Operands[1].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[2].getImm(), CurByte, OS);
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x46, CurByte,
             OS);
    break;
  case Z80::BIT8bp:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    switch (Operands[1].getReg()) {
    case Z80::HL:
      emitByte(0xcbU, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x46U,
             CurByte, OS);
    break;
  case Z80::CALL16:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    emitByte(0xcdU, CurByte, OS);
    if (Operands[0].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Operand should be an expression or immediate.");
    }
    break;
  case Z80::CALL16CC:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[1].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be in range 0..7.");
    }
    emitByte((static_cast<uint8_t>(Operands[1].getImm()) << 3U) | 0xc4U,
             CurByte, OS);
    if (Operands[0].isExpr()) {
      if (!(MCExpr::SymbolRef == (Operands[0].getExpr()->getKind())))
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Fitst operand expression should be a call target.");
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "First operand should be an expression or immediate.");
    }
    break;
  case Z80::CCF:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x3fU, CurByte, OS);
    break;
  case Z80::CP8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xfeU, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::CP8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xbeU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::CP8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xbeU, CurByte, OS);
    break;
  case Z80::CP8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xbfU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xb8U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xb9U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xbaU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xbbU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xbcU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xbdU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xbcU, CurByte, OS); // CP A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xbdU, CurByte, OS); // CP A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xbcU, CurByte, OS); // CP A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xbdU, CurByte, OS); // CP A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::CPD16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa9U, CurByte, OS);
    break;
  case Z80::CPDR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb9U, CurByte, OS);
    break;
  case Z80::CPI16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa1U, CurByte, OS);
    break;
  case Z80::CPIR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb1U, CurByte, OS);
    break;
  case Z80::CPL:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x2fU, CurByte, OS);
    break;
  case Z80::DEC16SP:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x3bU, CurByte, OS);
    break;
  case Z80::DEC16r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0x0bU, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0x1bU, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0x2bU, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x2bU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x2bU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    break;
  case Z80::DEC8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x35U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::DEC8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      emitByte(0x35U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x35U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x35U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    break;
  case Z80::DEC8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x3dU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x05U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x0dU, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x15U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x1dU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x25U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x2dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x25U, CurByte, OS); // DEC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2dU, CurByte, OS); // DEC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x25U, CurByte, OS); // DEC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2dU, CurByte, OS); // DEC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::DI:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xf3U, CurByte, OS);
    break;
  case Z80::EI:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xfbU, CurByte, OS);
    break;
  case Z80::EX16DE:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xebU, CurByte, OS);
    break;
  case Z80::EX16SP:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    if ((Operands[0].getReg()) != (Operands[1].getReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be the same register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0xe3U, CurByte, OS);
    break;
  case Z80::EXAF:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x08U, CurByte, OS);
    break;
  case Z80::EXX:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xd9U, CurByte, OS);
    break;
  case Z80::INC16SP:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x33U, CurByte, OS);
    break;
  case Z80::INC16r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0x03U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0x13U, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0x23U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x23U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x23U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    break;
  case Z80::INC8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x34U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::INC8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      emitByte(0x34U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x34U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x34U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    break;
  case Z80::INC8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x3cU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x04U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x0cU, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x14U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x1cU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x24U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x2cU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x24U, CurByte, OS); // INC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2cU, CurByte, OS); // INC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x24U, CurByte, OS); // INC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2cU, CurByte, OS); // INC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::IND16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xaaU, CurByte, OS);
    break;
  case Z80::INDR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xbaU, CurByte, OS);
    break;
  case Z80::INI16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa2U, CurByte, OS);
    break;
  case Z80::INIR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb2U, CurByte, OS);
    break;
  case Z80::JP16r:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0xe9U, CurByte, OS);
    break;
  case Z80::LD16SP:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0xf9U, CurByte, OS);
    break;
  case Z80::LD16am:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0x2aU, CurByte, OS);
    if (Operands[1].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[1].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[1].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Second operand should be an expression or immediate.");
    }
    break;
  case Z80::LD16ma:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::HL:
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(0x22U, CurByte, OS);
    if (Operands[0].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "First operand should be an expression or immediate.");
    }
    break;
  case Z80::LD16mo:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::BC:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x43U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x53U, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x63U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x22U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x22U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    if (Operands[0].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "First operand should be an expression or immediate.");
    }
    break;
  case Z80::LD16om:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x4bU, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x5bU, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0xedU, CurByte, OS);
      emitByte(0x6bU, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x2aU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x2aU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    if (Operands[1].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[1].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[1].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Second operand should be an expression or immediate.");
    }
    break;
  case Z80::LD16ri:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0x01U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0x11U, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0x21U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x21U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x21U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    if (Operands[1].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[1].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[1].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[1].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Second operand should be an expression or immediate.");
    }
    break;
  case Z80::LD8am:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    emitByte(0x3aU, CurByte, OS);
    if (Operands[0].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Operand should be an expression or immediate.");
    }
    break;
  case Z80::LD8gg:
  case Z80::LD8xx:
  case Z80::LD8yy:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    switch (Operands[0].getReg()) { /* OUT: r */
    case Z80::A:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x7fU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x78U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x79U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x7aU, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x7bU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x7cU, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x7dU, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x7cU, CurByte, OS); // LD A, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x7dU, CurByte, OS); // LD A, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x7cU, CurByte, OS); // LD A, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x7dU, CurByte, OS); // LD A, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::B:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x47U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x40U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x41U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x42U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x43U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x44U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x45U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x44U, CurByte, OS); // LD B, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x45U, CurByte, OS); // LD B, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x44U, CurByte, OS); // LD B, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x45U, CurByte, OS); // LD B, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::C:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x4fU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x48U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x49U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x4aU, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x4bU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x4cU, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x4dU, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x4cU, CurByte, OS); // LD C, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x4dU, CurByte, OS); // LD C, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x4cU, CurByte, OS); // LD C, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x4dU, CurByte, OS); // LD C, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::D:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x57U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x50U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x51U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x52U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x53U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x54U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x55U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x54U, CurByte, OS); // LD D, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x55U, CurByte, OS); // LD D, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x54U, CurByte, OS); // LD D, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x55U, CurByte, OS); // LD D, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::E:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x5fU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x58U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x59U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x5aU, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x5bU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x5cU, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x5dU, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x5cU, CurByte, OS); // LD E, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x5dU, CurByte, OS); // LD E, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x5cU, CurByte, OS); // LD E, H
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x5dU, CurByte, OS); // LD E, L
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::H:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x67U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x60U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x61U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x62U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x63U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x64U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x65U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::L:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0x6fU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x68U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x69U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x6aU, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x6bU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x6cU, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x6dU, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IXH:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x67U, CurByte, OS); // LD H, A
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::B:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x60U, CurByte, OS); // LD H, B
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::C:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x61U, CurByte, OS); // LD H, C
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::D:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::E:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::H:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x54U, CurByte, OS); // LD D, H
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::L:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x55U, CurByte, OS); // LD D, L
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x64U, CurByte, OS); // LD H, H
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x65U, CurByte, OS); // LD H, L
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IXL:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6fU, CurByte, OS); // LD L, A
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::B:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x68U, CurByte, OS); // LD L, B
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::C:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x69U, CurByte, OS); // LD L, C
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::D:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::E:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::H:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5cU, CurByte, OS); // LD E, H
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::L:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5dU, CurByte, OS); // LD E, L
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6cU, CurByte, OS); // LD L, H
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6dU, CurByte, OS); // LD L, L
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IYH:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x67U, CurByte, OS); // LD H, A
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::B:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x60U, CurByte, OS); // LD H, B
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::C:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x61U, CurByte, OS); // LD H, C
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::D:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::E:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::H:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x54U, CurByte, OS); // LD D, H
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::L:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x55U, CurByte, OS); // LD D, L
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x62U, CurByte, OS); // LD H, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x63U, CurByte, OS); // LD H, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x64U, CurByte, OS); // LD H, H
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x65U, CurByte, OS); // LD H, L
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IYL:
      switch (Operands[1].getReg()) { /* IN: r' */
      case Z80::A:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6fU, CurByte, OS); // LD L, A
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::B:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x68U, CurByte, OS); // LD L, B
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::C:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x69U, CurByte, OS); // LD L, C
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::D:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::E:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::H:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5cU, CurByte, OS); // LD E, H
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::L:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5dU, CurByte, OS); // LD E, L
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6aU, CurByte, OS); // LD L, D
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IXL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x6bU, CurByte, OS); // LD L, E
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYH:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6cU, CurByte, OS); // LD L, H
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IYL:
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        emitByte(0x6dU, CurByte, OS); // LD L, L
        emitByte(0xe5U, CurByte, OS); // PUSH HL
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      default:
        report_fatal_instr_problem(MCII.getName(opcode),
                                   "Allowed register are A, B, C, D, E, H, L.");
      }
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::LD8go:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First two operands should be registers.");
    if (!(Operands[2].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Third operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed second operand registers are IX, IY.");
      }
      emitByte(0x66U, CurByte, OS);
      emitByte(Operands[2].getImm(), CurByte, OS); // LD H, (IX|Y + Imm)
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed second operand registers are IX, IY.");
      }
      emitByte(0x6eU, CurByte, OS);
      emitByte(Operands[2].getImm(), CurByte, OS); // LD L, (IX|Y + Imm)
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed second operand registers are IX, IY.");
      }
      emitByte(0x66, CurByte, OS);
      emitByte(Operands[2].getImm(), CurByte, OS); // LD H, (IX|Y + Imm)
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed second operand registers are IX, IY.");
      }
      emitByte(0x6eU, CurByte, OS);
      emitByte(Operands[2].getImm(), CurByte, OS); // LD L, (IX|Y + Imm)
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed second operand registers are IX, IY.");
      }
      switch (Operands[0].getReg()) {
      case Z80::A:
        emitByte(0x7eU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x46U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x4eU, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x56U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x5eU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x66U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x6eU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      emitByte(Operands[2].getImm(), CurByte, OS);
    }
    break;
  case Z80::LD8gp:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    switch (Operands[1].getReg()) {
    case Z80::HL:
      switch (Operands[0].getReg()) {
      case Z80::A:
        emitByte(0x7eU, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x46U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x4eU, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x56U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x5eU, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x66U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x6eU, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x56U, CurByte, OS); // LD D, (HL)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5eU, CurByte, OS); // LD E, (HL)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x56U, CurByte, OS); // LD D, (HL)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x5eU, CurByte, OS); // LD E, (HL)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IX:
      switch (Operands[0].getReg()) {
      case Z80::A:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x7eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x46U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x4eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x66U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x6eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD D, (IX+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD E, (IX+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD D, (IX+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD E, (IX+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IY:
      switch (Operands[0].getReg()) {
      case Z80::A:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x7eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x46U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x4eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x66U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x6eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD D, (IY+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD E, (IY+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x56U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD D, (IY+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x5eU, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD E, (IY+0)
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    break;
  case Z80::LD8ma:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    emitByte(0x32U, CurByte, OS);
    if (Operands[0].isExpr()) {
      Fixups.push_back(MCFixup::create(CurByte, Operands[0].getExpr(),
                                       static_cast<MCFixupKind>(Z80::fixup_16),
                                       MI.getLoc()));
      emitByte(0x00U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
    } else if (Operands[0].isImm()) {
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 0U) & 0xffU,
               CurByte, OS);
      emitByte(((static_cast<uint16_t>(Operands[0].getImm())) >> 8U) & 0xffU,
               CurByte, OS);
    } else {
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Operand should be an expression or immediate.");
    }
    break;
  case Z80::LD8og:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[2].isReg())))
      report_fatal_instr_problem(
          MCII.getName(opcode), "First and third operand should be registers.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[2].getReg()) {
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[0].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are IX, IY.");
      }
      emitByte(0x74U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD (IX|Y + Imm), H
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[0].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are IX, IY.");
      }
      emitByte(0x75U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD (IX|Y + Imm), L
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[0].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are IX, IY.");
      }
      emitByte(0x74U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD (IX|Y + Imm), H
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      switch (Operands[0].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are IX, IY.");
      }
      emitByte(0x75U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD (IX|Y + Imm), L
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      switch (Operands[0].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are IX, IY.");
      }
      switch (Operands[2].getReg()) {
      case Z80::A:
        emitByte(0x77U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x70U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x71U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x72U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x73U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x74U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x75U, CurByte, OS);
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed third operand registers are A, B, C, D, E, H, L.");
      }
      emitByte(Operands[1].getImm(), CurByte, OS);
    }
    break;
  case Z80::LD8oi:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!((Operands[1].isImm()) && (Operands[2].isImm())))
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Second and third  operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x36U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(Operands[2].getImm(), CurByte, OS);
    break;
  case Z80::LD8pg:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Both operands should be registers.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      switch (Operands[1].getReg()) {
      case Z80::A:
        emitByte(0x77U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0x70U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0x71U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0x72U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0x73U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0x74U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0x75U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x72U, CurByte, OS); // LD (HL), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x73U, CurByte, OS); // LD (HL), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x72U, CurByte, OS); // LD (HL), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0x73U, CurByte, OS); // LD (HL), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IX:
      switch (Operands[1].getReg()) {
      case Z80::A:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x77U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x70U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x71U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x74U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0xddU, CurByte, OS);
        emitByte(0x75U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IX+0), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IX+0), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IX+0), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IX+0), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    case Z80::IY:
      switch (Operands[1].getReg()) {
      case Z80::A:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x77U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::B:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x70U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::C:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x71U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::D:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::E:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::H:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x74U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::L:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x75U, CurByte, OS);
        emitByte(0x00U, CurByte, OS);
        break;
      case Z80::IXH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IY+0), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IXL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IY+0), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYH:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x72U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IY+0), D
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::IYL:
        emitByte(0xd5U, CurByte, OS); // PUSH DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
        emitByte(0xd1U, CurByte, OS); // POP DE
        emitByte(0xfdU, CurByte, OS);
        emitByte(0x73U, CurByte, OS);
        emitByte(0x00U, CurByte, OS); // LD (IY+0), E
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed first operand registers are A, B, C, D, E, H, L.");
      }
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    break;
  case Z80::LD8ri:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x3eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x06U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x0eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x16U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x1eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x26U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x2eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x26U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD H, Imm
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD L, Imm
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x26U, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD H, Imm
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x2eU, CurByte, OS);
      emitByte(Operands[1].getImm(), CurByte, OS); // LD L, Imm
      emitByte(0xe5U, CurByte, OS);                // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Allowed first operand registers are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::LD8pi:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      emitByte(0x36U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0x36U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x36U, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are HL, IX, IY.");
    }
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::LDD16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa8U, CurByte, OS);
    break;
  case Z80::LDDR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb8U, CurByte, OS);
    break;
  case Z80::LDI16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa0U, CurByte, OS);
    break;
  case Z80::LDIR16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb0U, CurByte, OS);
    break;
  case Z80::LEA16ro:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!((Operands[0].isReg()) && (Operands[1].isReg())))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First two operands should be registers.");
    if (!(Operands[2].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Third operand should be immediate.");
    emitByte(0xf5U, CurByte, OS); // PUSH AF
    if (Z80::BC != Operands[0].getReg())
      emitByte(0xc5U, CurByte, OS); // PUSH BC
    emitByte(0x06U, CurByte, OS);
    emitByte(0x00U, CurByte, OS); // LD B, 0
    emitByte(0x0eU, CurByte, OS);
    emitByte(Operands[2].getImm(), CurByte, OS); // LD C, Imm
    switch (Operands[1].getReg()) {
    case Z80::IX:
      if ((Operands[0].getReg()) != (Operands[1].getReg())) {
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
      }
      emitByte(0xddU, CurByte, OS);
      emitByte(0x09U, CurByte, OS); // ADD IX, BC
      if ((Operands[0].getReg()) != (Operands[1].getReg())) {
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IX
      }
      break;
    case Z80::IY:
      if ((Operands[0].getReg()) != (Operands[1].getReg())) {
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
      }
      emitByte(0xfdU, CurByte, OS);
      emitByte(0x09U, CurByte, OS); // ADD IY, BC
      if ((Operands[0].getReg()) != (Operands[1].getReg())) {
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe5U, CurByte, OS); // PUSH IY
      }
      break;
    default:
      report_fatal_instr_problem(
          MCII.getName(opcode),
          "Allowed registers in the second operand are IX, IY.");
    }
    if ((Operands[0].getReg()) != (Operands[1].getReg())) {
      switch (Operands[0].getReg()) {
      case Z80::BC:
        emitByte(0xc1U, CurByte, OS); // POP BC
        break;
      case Z80::DE:
        emitByte(0xd1U, CurByte, OS); // POP DE
        break;
      case Z80::HL:
        emitByte(0xe1U, CurByte, OS); // POP HL
        break;
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed registers in the first operand are BC, DE, HL, IX, IY.");
      }
      switch (Operands[1].getReg()) {
      case Z80::IX:
        emitByte(0xddU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IX
        break;
      case Z80::IY:
        emitByte(0xfdU, CurByte, OS);
        emitByte(0xe1U, CurByte, OS); // POP IY
        break;
      default:
        report_fatal_instr_problem(
            MCII.getName(opcode),
            "Allowed registers in the second operand are IX, IY.");
      }
    }
    if (Z80::BC != Operands[0].getReg())
      emitByte(0xc1U, CurByte, OS); // POP BC
    emitByte(0xf1U, CurByte, OS);   // POP AF
    break;
  case Z80::NEG:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0x44U, CurByte, OS);
    break;
  case Z80::NOP:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x00U, CurByte, OS);
    break;
  case Z80::OR8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xf6U, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::OR8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xb6U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::OR8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xb6U, CurByte, OS);
    break;
  case Z80::OR8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xb7U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xb0U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xb1U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xb2U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xb3U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xb4U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xb5U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xb4U, CurByte, OS); // OR A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xb5U, CurByte, OS); // OR A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xb4U, CurByte, OS); // OR A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xb5U, CurByte, OS); // OR A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::OUTD16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xabU, CurByte, OS);
    break;
  case Z80::OUTDR16: /* OTDR */
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xbbU, CurByte, OS);
    break;
  case Z80::OUTI16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xa3U, CurByte, OS);
    break;
  case Z80::OUTIR16: /* OTIR */
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0xb3U, CurByte, OS);
    break;
  case Z80::POP16AF:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xf1U, CurByte, OS);
    break;
  case Z80::POP16r:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0xc1U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0xd1U, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0xe1U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    break;
  case Z80::PUSH16AF:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xf5U, CurByte, OS);
    break;
  case Z80::PUSH16r:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0xc5U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0xd5U, CurByte, OS);
      break;
    case Z80::HL:
      emitByte(0xe5U, CurByte, OS);
      break;
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE, HL, IX, IY.");
    }
    break;
  case Z80::RES8bg:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x87U,
               CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x80U,
               CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x81U,
               CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x82U,
               CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x83U,
               CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x84U,
               CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x85U,
               CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x84U,
               CurByte, OS);        // RESn A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x85U,
               CurByte, OS);        // RESn A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x84U,
               CurByte, OS);        // RESn A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x85U,
               CurByte, OS);        // RESn A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::RES8bo:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    if (!(Operands[2].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Third operand should be immediate.");
    switch (Operands[1].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[2].getImm(), CurByte, OS);
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x86U,
             CurByte, OS);
    break;
  case Z80::RES8bp:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0x86U,
             CurByte, OS);
    break;
  case Z80::RET16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xc9U, CurByte, OS);
    break;
  case Z80::RET16CC:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be in range 0..7.");
    }
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc0U,
             CurByte, OS);
    break;
  case Z80::RETI16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0x4dU, CurByte, OS);
    break;
  case Z80::RETN16:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0x45U, CurByte, OS);
    break;
  case Z80::RL8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x16U, CurByte, OS);
    break;
  case Z80::RL8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x16U, CurByte, OS);
    break;
  case Z80::RL8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x17U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x10U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x11U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x12U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x13U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x14U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x15U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x14U, CurByte, OS); // RL H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x15U, CurByte, OS); // RL L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x14U, CurByte, OS); // RL H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x15U, CurByte, OS); // RL L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::RLC8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x06U, CurByte, OS);
    break;
  case Z80::RLC8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x06U, CurByte, OS);
    break;
  case Z80::RLC8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x07U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x00U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x01U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x02U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x03U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x04U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x05U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x04U, CurByte, OS); // RLC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x05U, CurByte, OS); // RLC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x04U, CurByte, OS); // RLC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x05U, CurByte, OS); // RLC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::RR8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x1eU, CurByte, OS);
    break;
  case Z80::RR8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x1eU, CurByte, OS);
    break;
  case Z80::RR8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x18U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x19U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1cU, CurByte, OS); // RR H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1dU, CurByte, OS); // RR L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1cU, CurByte, OS); // RR H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x1dU, CurByte, OS); // RR L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::RRC8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x0eU, CurByte, OS);
    break;
  case Z80::RRC8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x0eU, CurByte, OS);
    break;
  case Z80::RRC8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x08U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x09U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0cU, CurByte, OS); // RRC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0dU, CurByte, OS); // RRC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0cU, CurByte, OS); // RRC H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x0dU, CurByte, OS); // RRC L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SBC16SP:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0x72U, CurByte, OS);
    break;
  case Z80::SBC16aa:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0xedU, CurByte, OS);
    emitByte(0x62U, CurByte, OS);
    break;
  case Z80::SBC16ao:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    emitByte(0xedU, CurByte, OS);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::BC:
      emitByte(0x42U, CurByte, OS);
      break;
    case Z80::DE:
      emitByte(0x52U, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are BC, DE.");
    }
    break;
  case Z80::SBC8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xdeU, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::SBC8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x9eU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::SBC8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0x9eU, CurByte, OS);
    break;
  case Z80::SBC8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x9fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x98U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x99U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x9aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x9bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x9cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x9dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x9cU, CurByte, OS); // SBC A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x9dU, CurByte, OS); // SBC A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x9cU, CurByte, OS); // SBC A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x9dU, CurByte, OS); // SBC A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SCF:
    check_num_operands(MCII.getName(opcode), 0U, numOperands);
    emitByte(0x37U, CurByte, OS);
    break;
  case Z80::SET8bg:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc7U,
               CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc0U,
               CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc1U,
               CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc2U,
               CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc3U,
               CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc4U,
               CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc5U,
               CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc4U,
               CurByte, OS);        // SETn A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc5U,
               CurByte, OS);        // SETn A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc4U,
               CurByte, OS);        // SETn A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc5U,
               CurByte, OS);        // SETn A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SET8bo:
    check_num_operands(MCII.getName(opcode), 3U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    if (!(Operands[2].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Third operand should be immediate.");
    switch (Operands[1].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[2].getImm(), CurByte, OS);
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc6U,
             CurByte, OS);
    break;
  case Z80::SET8bp:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be immediate.");
    if (8U <= static_cast<uint8_t>(Operands[0].getImm())) {
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be in range 0..7.");
    }
    if (!(Operands[1].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be register.");
    switch (Operands[1].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte((static_cast<uint8_t>(Operands[0].getImm()) << 3U) | 0xc6U,
             CurByte, OS);
    break;
  case Z80::SLA8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x26U, CurByte, OS);
    break;
  case Z80::SLA8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x26U, CurByte, OS);
    break;
  case Z80::SLA8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x27U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x20U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x21U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x22U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x23U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x24U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x25U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x24U, CurByte, OS); // SLA H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x25U, CurByte, OS); // SLA L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x24U, CurByte, OS); // SLA H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x25U, CurByte, OS); // SLA L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SRA8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x2eU, CurByte, OS);
    break;
  case Z80::SRA8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x2eU, CurByte, OS);
    break;
  case Z80::SRA8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x28U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x29U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2cU, CurByte, OS); // SRA H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2dU, CurByte, OS); // SRA L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2cU, CurByte, OS); // SRA H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x2dU, CurByte, OS); // SRA L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SRL8o:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    emitByte(0x3eU, CurByte, OS);
    break;
  case Z80::SRL8p:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xcbU, CurByte, OS);
    emitByte(0x3eU, CurByte, OS);
    break;
  case Z80::SRL8r:
    if (!numOperands)
      report_fatal_instr_problem(MCII.getName(opcode), "Operand missing.");
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "An operand should be an register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3fU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x38U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x39U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3aU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3bU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3cU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3dU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3cU, CurByte, OS); // SRL H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3dU, CurByte, OS); // SRL L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3cU, CurByte, OS); // SRL H
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xcbU, CurByte, OS);
      emitByte(0x3dU, CurByte, OS); // SRL L
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe1U, CurByte, OS); // POP IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::SUB8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xd6U, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::SUB8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0x96U, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::SUB8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0x96U, CurByte, OS);
    break;
  case Z80::SUB8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0x97U, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0x90U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0x91U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0x92U, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0x93U, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0x94U, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0x95U, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x94U, CurByte, OS); // SUB A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x95U, CurByte, OS); // SUB A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x94U, CurByte, OS); // SUB A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0x95U, CurByte, OS); // SUB A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::XOR8ai:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be immediate.");
    emitByte(0xeeU, CurByte, OS);
    emitByte(Operands[0].getImm(), CurByte, OS);
    break;
  case Z80::XOR8ao:
    check_num_operands(MCII.getName(opcode), 2U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "First operand should be register.");
    if (!(Operands[1].isImm()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Second operand should be immediate.");
    switch (Operands[0].getReg()) {
    case Z80::IX:
      emitByte(0xddU, CurByte, OS);
      break;
    case Z80::IY:
      emitByte(0xfdU, CurByte, OS);
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed registers are IX, IY.");
    }
    emitByte(0xaeU, CurByte, OS);
    emitByte(Operands[1].getImm(), CurByte, OS);
    break;
  case Z80::XOR8ap:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::HL:
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "The only allowed register is HL.");
    }
    emitByte(0xaeU, CurByte, OS);
    break;
  case Z80::XOR8ar:
    check_num_operands(MCII.getName(opcode), 1U, numOperands);
    if (!(Operands[0].isReg()))
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Operand should be register.");
    switch (Operands[0].getReg()) {
    case Z80::A:
      emitByte(0xafU, CurByte, OS);
      break;
    case Z80::B:
      emitByte(0xa8U, CurByte, OS);
      break;
    case Z80::C:
      emitByte(0xa9U, CurByte, OS);
      break;
    case Z80::D:
      emitByte(0xaaU, CurByte, OS);
      break;
    case Z80::E:
      emitByte(0xabU, CurByte, OS);
      break;
    case Z80::H:
      emitByte(0xacU, CurByte, OS);
      break;
    case Z80::L:
      emitByte(0xadU, CurByte, OS);
      break;
    case Z80::IXH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xacU, CurByte, OS); // XOR A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IXL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xddU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IX
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xadU, CurByte, OS); // XOR A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYH:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xacU, CurByte, OS); // XOR A, H
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    case Z80::IYL:
      emitByte(0xe5U, CurByte, OS); // PUSH HL
      emitByte(0xfdU, CurByte, OS);
      emitByte(0xe5U, CurByte, OS); // PUSH IY
      emitByte(0xe1U, CurByte, OS); // POP HL
      emitByte(0xadU, CurByte, OS); // XOR A, L
      emitByte(0xe1U, CurByte, OS); // POP HL
      break;
    default:
      report_fatal_instr_problem(MCII.getName(opcode),
                                 "Allowed register are A, B, C, D, E, H, L.");
    }
    break;
  case Z80::ADC16SP:
  case Z80::ADC16aa:
  case Z80::ADC16ao:
  case Z80::JP16:
  case Z80::JP16CC:
  case Z80::JR:
  case Z80::JRCC:
  case Z80::LD16or:
  case Z80::LD16pr:
  case Z80::LD16ro:
  case Z80::LD16rp:
    report_fatal_instr_problem(MCII.getName(opcode), "Not implemented.");
    break;
  default:
    std::string msg;
    raw_string_ostream Msg(msg);
    Msg << "Not supported instr: " << MCII.getName(opcode) << ' ' << MI;
    report_fatal_error(Msg.str());
  }
}

} // end of namespace llvm
