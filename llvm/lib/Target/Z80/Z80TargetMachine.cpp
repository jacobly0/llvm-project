//===-- Z80TargetMachine.cpp - Define TargetMachine for the Z80 -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the Z80 specific subclass of TargetMachine.
//
//===----------------------------------------------------------------------===//

#include "Z80TargetMachine.h"
#include "TargetInfo/Z80TargetInfo.h"
#include "Z80.h"
#include "Z80Subtarget.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

extern "C" void LLVMInitializeZ80Target() {
  // Register the target.
  RegisterTargetMachine<Z80TargetMachine> X(getTheZ80Target());
  RegisterTargetMachine<Z80TargetMachine> Y(getTheEZ80Target());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeGlobalISel(PR);
}

static std::string computeDataLayout(const Triple &TT) {
  bool IsEZ80 = TT.getArch() == Triple::ez80;
  bool Is16Bit = TT.isArch16Bit() || TT.getEnvironment() == Triple::CODE16;
  // Z80 is little endian and mangling is closest to MachO.
  std::string Ret = "e-m:o";
  // Memory Address Width
  Ret += Is16Bit ? "-p:16:8" : "-p:24:8";
  // Port Address Width
  Ret += IsEZ80 ? "-p1:16:8" : "-p1:8:8";
  // Other Address Width
  if (IsEZ80)
    Ret += Is16Bit ? "-p2:24:8" : "-p2:16:8";
  Ret += "-i16:8-i24:8-i32:8-i48:8-i64:8-i96:8-f32:8-f64:8-a:8-n8:16";
  if (!Is16Bit)
    Ret += ":24";
  Ret += "-S8";
  return Ret;
}

static Reloc::Model getEffectiveRelocModel(Optional<Reloc::Model> RM) {
  if (RM)
    return *RM;
  return Reloc::Static;
}

/// Create a Z80 target.
///
Z80TargetMachine::Z80TargetMachine(const Target &T, const Triple &TT,
                                   StringRef CPU, StringRef FS,
                                   const TargetOptions &Options,
                                   Optional<Reloc::Model> RM,
                                   Optional<CodeModel::Model> CM,
                                   CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT), TT, CPU, FS, Options,
                        getEffectiveRelocModel(RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<TargetLoweringObjectFileOMF>()) {
  initAsmInfo();

  setGlobalISel(true);
  setGlobalISelAbort(GlobalISelAbortMode::Enable);
}

Z80TargetMachine::~Z80TargetMachine() {}

const Z80Subtarget *
Z80TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef CPU = !CPUAttr.hasAttribute(Attribute::None)
                      ? CPUAttr.getValueAsString()
                      : (StringRef)TargetCPU;
  StringRef FS = !FSAttr.hasAttribute(Attribute::None)
                     ? FSAttr.getValueAsString()
                     : (StringRef)TargetFS;

  SmallString<512> Key;
  Key.reserve(CPU.size() + FS.size());
  Key += CPU;
  Key += FS;

  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<Z80Subtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
/// Z80 Code Generator Pass Configuration Options.
class Z80PassConfig : public TargetPassConfig {
public:
  Z80PassConfig(Z80TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  Z80TargetMachine &getZ80TargetMachine() const {
    return getTM<Z80TargetMachine>();
  }

  bool addIRTranslator() override;
  bool addLegalizeMachineIR() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addFastRegAlloc() override;
};
} // end anonymous namespace

TargetPassConfig *Z80TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new Z80PassConfig(*this, PM);
}

bool Z80PassConfig::addIRTranslator() {
  addPass(new IRTranslator);
  return false;
}

bool Z80PassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer);
  return false;
}

bool Z80PassConfig::addRegBankSelect() {
  addPass(new RegBankSelect);
  return false;
}

bool Z80PassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect);
  return false;
}

void Z80PassConfig::addFastRegAlloc() {
  // FastRegAlloc can't handle the register pressure on the Z80
  if (usingDefaultRegAlloc())
    addOptimizedRegAlloc();
  else
    TargetPassConfig::addFastRegAlloc();
}
