//===- lib/MC/MCOMFStreamer.cpp - OMF Object Output -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file assembles .s files and emits OMF .o object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCOMFStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

bool MCOMFStreamer::EmitSymbolAttribute(MCSymbol *Symbol,
                                        MCSymbolAttr Attribute) {
  llvm_unreachable("Unimplemented!");
}

void MCOMFStreamer::EmitCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                     unsigned ByteAlignment) {
  llvm_unreachable("Unimplemented!");
}

void MCOMFStreamer::EmitZerofill(MCSection *Section, MCSymbol *Symbol,
                                 uint64_t Size, unsigned ByteAlignment,
                                 SMLoc Loc) {
  llvm_unreachable("Unimplemented!");
}

void MCOMFStreamer::EmitInstToData(const MCInst &Inst,
                                   const MCSubtargetInfo &) {
  llvm_unreachable("Unimplemented!");
}

MCStreamer *llvm::createOMFStreamer(MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&CE,
                                    bool RelaxAll) {
  return new MCOMFStreamer(Context, std::move(MAB), std::move(OW), std::move(CE));
}
