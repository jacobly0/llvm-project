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
#include "Z80InstPrinter.h"
#include "Z80MCAsmInfo.h"
#include "Z80TargetStreamer.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/TargetRegistry.h"
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
      ArchFS = FS;
  }

  return createZ80MCSubtargetInfoImpl(TT, CPU, ArchFS);
}

static MCAsmInfo *createZ80MCAsmInfo(const MCRegisterInfo &MRI,
                                     const Triple &TheTriple,
                                     const MCTargetOptions &Options) {
  return new Z80MCAsmInfo(TheTriple);
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
                           MCInstPrinter */*InstPrint*/,
                           bool /*isVerboseAsm*/) {
  return new Z80TargetAsmStreamer(S, OS);
}

// Force static initialization.
extern "C" void LLVMInitializeZ80TargetMC() {
  for (Target *T : {&getTheZ80Target(), &getTheEZ80Target()}) {
    // Register the MC asm info.
    RegisterMCAsmInfoFn X(*T, createZ80MCAsmInfo);

    // Register the MCInstPrinter.
    TargetRegistry::RegisterMCInstPrinter(*T, createZ80MCInstPrinter);

    // Register the asm target streamer.
    TargetRegistry::RegisterAsmTargetStreamer(*T, createZ80AsmTargetStreamer);
  }
}
