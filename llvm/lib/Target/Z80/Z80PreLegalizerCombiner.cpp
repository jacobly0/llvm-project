//=== lib/CodeGen/GlobalISel/Z80PreLegalizerCombiner.cpp ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "Z80.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "z80-prelegalizer-combiner"

using namespace llvm;

#define Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_DEPS
#include "Z80GenGICombiner.inc"
#undef Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_DEPS

namespace {
#define Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_H
#include "Z80GenGICombiner.inc"
#undef Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_H

class Z80PreLegalizerCombinerInfo : public CombinerInfo {
  GISelKnownBits *KB;
  MachineDominatorTree *MDT;

public:
  Z80GenPreLegalizerCombinerHelper Generated;

  Z80PreLegalizerCombinerInfo(bool EnableOpt, bool OptSize, bool MinSize,
                              GISelKnownBits *KB, MachineDominatorTree *MDT)
      : CombinerInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, OptSize, MinSize),
        KB(KB), MDT(MDT) {
    if (!Generated.parseCommandLineOption())
      report_fatal_error("Invalid rule identifier");
  }

  virtual bool combine(GISelChangeObserver &Observer, MachineInstr &MI,
                       MachineIRBuilder &B) const override;
};

bool Z80PreLegalizerCombinerInfo::combine(GISelChangeObserver &Observer,
                                          MachineInstr &MI,
                                          MachineIRBuilder &B) const {
  CombinerHelper Helper(Observer, B, KB, MDT);
  return Generated.tryCombineAll(Observer, MI, B, Helper);
}

#define Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_CPP
#include "Z80GenGICombiner.inc"
#undef Z80PRELEGALIZERCOMBINERHELPER_GENCOMBINERHELPER_CPP

// Pass boilerplate
// ================

class Z80PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  Z80PreLegalizerCombiner(bool IsOptNone = false);

  StringRef getPassName() const override { return "Z80 Pre-Legalizer Combiner"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
private:
  bool IsOptNone;
};
} // end anonymous namespace

void Z80PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  if (!IsOptNone) {
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
  }
  MachineFunctionPass::getAnalysisUsage(AU);
}

Z80PreLegalizerCombiner::Z80PreLegalizerCombiner(bool IsOptNone)
    : MachineFunctionPass(ID), IsOptNone(IsOptNone) {
  initializeZ80PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());
}

bool Z80PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto *TPC = &getAnalysis<TargetPassConfig>();
  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOpt::None && !skipFunction(F);
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);
  MachineDominatorTree *MDT =
      IsOptNone ? nullptr : &getAnalysis<MachineDominatorTree>();
  Z80PreLegalizerCombinerInfo PCInfo(EnableOpt, F.hasOptSize(), F.hasMinSize(),
                                     KB, MDT);
  Combiner C(PCInfo, TPC);
  return C.combineMachineInstrs(MF, /*CSEInfo*/ nullptr);
}

char Z80PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(Z80PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine Z80 machine instrs before legalization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_END(Z80PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine Z80 machine instrs before legalization", false,
                    false)


namespace llvm {
FunctionPass *createZ80PreLegalizeCombiner(bool IsOptNone) {
  return new Z80PreLegalizerCombiner(IsOptNone);
}
} // end namespace llvm
