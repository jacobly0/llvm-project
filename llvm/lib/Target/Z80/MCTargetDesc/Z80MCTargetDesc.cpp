//===-- Z80MCTargetDesc.cpp - Z80 Target Descriptions ---------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Z80 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "Z80MCTargetDesc.h"
#include "EZ80InstPrinter.h"
#include "Z80AsmBackend.h"
#include "Z80ELFStreamer.h"
#include "Z80InstPrinter.h"
#include "Z80MCAsmInfo.h"
#include "Z80MCCodeEmitter.h"
#include "Z80TargetStreamer.h"
#include "llvm/ADT/Triple.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/TargetRegistry.h"

#include <memory>
#include <utility>

using namespace llvm;

#define GET_REGINFO_MC_DESC
#include "Z80GenRegisterInfo.inc"

#define GET_INSTRINFO_MC_DESC
#include "Z80GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "Z80GenSubtargetInfo.inc"

std::string Z80_MC::ParseZ80Triple(const Triple &TT) {
  std::string FS;
  if (TT.getArch() == Triple::ez80)
    FS = "+24bit-mode,-16bit-mode";
  else
    FS = "-24bit-mode,+16bit-mode";

  return FS;
}

MCSubtargetInfo *Z80_MC::createZ80MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  std::string ArchFS = Z80_MC::ParseZ80Triple(TT);
  if (!FS.empty()) {
    if (!ArchFS.empty())
      ArchFS = (Twine(ArchFS) + "," + FS).str();
    else
      ArchFS = FS.str();
  }

  return createZ80MCSubtargetInfoImpl(TT, CPU, ArchFS);
}

static MCAsmInfo *createZ80MCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple,
                                     const MCTargetOptions &Options) {
  return new Z80MCAsmInfo(TheTriple);
}

MCInstrInfo *createZ80MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitZ80MCInstrInfo(X);

  return X;
}

static MCRegisterInfo *createZ80MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitZ80MCRegisterInfo(X, 0);

  return X;
}

static MCInstPrinter *createZ80MCInstPrinter(const Triple &T,
                                             unsigned SyntaxVariant,
                                             const MCAsmInfo &MAI,
                                             const MCInstrInfo &MII,
                                             const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new Z80InstPrinter(MAI, MII, MRI);
  if (SyntaxVariant == 1)
    return new Z80EInstPrinter(MAI, MII, MRI);
  return nullptr;
}

static MCTargetStreamer *
createZ80AsmTargetStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                           MCInstPrinter * /*InstPrint*/,
                           bool /*isVerboseAsm*/) {
  return new Z80TargetAsmStreamer(S, OS);
}

static MCCodeEmitter *createZ80MCCodeEmitter(const MCInstrInfo &MCII,
                                             const MCRegisterInfo &MRI,
                                             MCContext &Ctx) {
  return new Z80MCCodeEmitter(MCII, Ctx);
}

static MCStreamer *createMCStreamer(const Triple &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter,
                                    bool RelaxAll) {
  return createELFStreamer(Context, std::move(MAB), std::move(OW),
                           std::move(Emitter), RelaxAll);
}

static MCTargetStreamer *
createZ80ObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new Z80ELFStreamer(S, STI);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeZ80TargetMC() {
  for (Target *T : {&getTheZ80Target(), &getTheEZ80Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createZ80MCAsmInfo);

    // Register the MC instruction info.
    TargetRegistry::RegisterMCInstrInfo(*T, createZ80MCInstrInfo);

    // Register the MC register info.
    TargetRegistry::RegisterMCRegInfo(*T, createZ80MCRegisterInfo);

    // Register the MC subtarget info.
    TargetRegistry::RegisterMCSubtargetInfo(*T,
                                            Z80_MC::createZ80MCSubtargetInfo);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createZ80MCInstPrinter);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createZ80AsmTargetStreamer);
  }
  // Register the MC Code Emitter.
  TargetRegistry::RegisterMCCodeEmitter(getTheZ80Target(),
                                        createZ80MCCodeEmitter);

  // Register the obj streamer.
  TargetRegistry::RegisterELFStreamer(getTheZ80Target(), createMCStreamer);

  // Register the obj target streamer.
  TargetRegistry::RegisterObjectTargetStreamer(getTheZ80Target(),
                                               createZ80ObjectTargetStreamer);

  // Register the asm backend (as little endian).
  TargetRegistry::RegisterMCAsmBackend(getTheZ80Target(), createZ80AsmBackend);
}
