//===-- Z80MCInstLower.cpp - Convert Z80 MachineInstr to an MCInst --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains code to lower Z80 MachineInstrs to their corresponding
// MCInst records.
//
//===----------------------------------------------------------------------===//

#include "Z80AsmPrinter.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Debug.h"
using namespace llvm;

#define DEBUG_TYPE "z80-mclower"

namespace {
  /// Z80MCInstLower - This class is used to lower a MachineInstr into an MCInst.
  class Z80MCInstLower {
    MCContext &Ctx;
    const MachineFunction &Func;
    Z80AsmPrinter &AsmPrinter;

  public:
    Z80MCInstLower(const MachineFunction &MF, Z80AsmPrinter &AP);

    Optional<MCOperand> LowerMachineOperand(const MachineInstr *MI,
                                            const MachineOperand &MO) const;

    MCSymbol *GetGlobalAddressSymbol(const MachineOperand &MO) const;
    MCSymbol *GetExternalSymbolSymbol(const MachineOperand &MO) const;
    MCOperand LowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

    void Lower(const MachineInstr *MI, MCInst &OutMI) const;
  };
}

Z80MCInstLower::Z80MCInstLower(const MachineFunction &MF, Z80AsmPrinter &AP)
    : Ctx(MF.getContext()), Func(MF), AsmPrinter(AP) {}

/// GetGlobalAddressSymbol - Lower an MO_GlobalAddress operand to an MCSymbol.
MCSymbol *
Z80MCInstLower::GetGlobalAddressSymbol(const MachineOperand &MO) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  return AsmPrinter.getSymbol(MO.getGlobal());
}

/// GetExternalSymbolSymbol - Lower an MO_ExternalSymbol operand to an MCSymbol.
MCSymbol *
Z80MCInstLower::GetExternalSymbolSymbol(const MachineOperand &MO) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  return AsmPrinter.GetExternalSymbolSymbol(MO.getSymbolName());
}

MCOperand Z80MCInstLower::LowerSymbolOperand(const MachineOperand &MO,
                                             MCSymbol *Sym) const {
  assert(!MO.getTargetFlags() && "Unknown target flag on GV operand");
  const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Ctx);
  if (!MO.isJTI() && MO.getOffset())
    Expr = MCBinaryExpr::createAdd(
        Expr, MCConstantExpr::create(MO.getOffset(), Ctx), Ctx);
  return MCOperand::createExpr(Expr);
}

Optional<MCOperand>
Z80MCInstLower::LowerMachineOperand(const MachineInstr *MI,
                                    const MachineOperand &MO) const {
  switch (MO.getType()) {
  default:
    LLVM_DEBUG(MI->dump());
    llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    return MCOperand::createReg(MO.getReg());
  case MachineOperand::MO_Immediate:
    return MCOperand::createImm(MO.getImm());
  case MachineOperand::MO_CImmediate:
    return MCOperand::createImm(MO.getCImm()->getSExtValue());
  case MachineOperand::MO_MachineBasicBlock:
    return MCOperand::createExpr(MCSymbolRefExpr::create(
                       MO.getMBB()->getSymbol(), Ctx));
  case MachineOperand::MO_GlobalAddress:
    return LowerSymbolOperand(MO, GetGlobalAddressSymbol(MO));
  case MachineOperand::MO_ExternalSymbol:
    return LowerSymbolOperand(MO, GetExternalSymbolSymbol(MO));
  case MachineOperand::MO_JumpTableIndex:
    return LowerSymbolOperand(MO, AsmPrinter.GetJTISymbol(MO.getIndex()));
  case MachineOperand::MO_RegisterMask:
    return None; // Ignore call clobbers.
  }
}

void Z80MCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.setOpcode(MI->getOpcode());

  MCOperand MCOp;
  for (const MachineOperand &MO : MI->operands())
    if (auto PossibleMCOp = LowerMachineOperand(MI, MO))
      OutMI.addOperand(*PossibleMCOp);
}

void Z80AsmPrinter::EmitInstruction(const MachineInstr *MI) {
  Z80MCInstLower MCInstLowering(*MF, *this);

  MCInst TmpInst;
  MCInstLowering.Lower(MI, TmpInst);
  EmitToStreamer(*OutStreamer, TmpInst);
}
