//===-- Z80.td - Target definition file for the Zilog Z80 --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is a target description file for the Zilog z80 architecture, referred
// to here as the "Z80" architecture.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing...
//
include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Z80 Subtarget features
//===----------------------------------------------------------------------===//

def FeatureUndoc   : SubtargetFeature<"undoc", "HasUndocOps", "true",
                                      "Enable undocumented z80 instructions">;
def FeatureZ180    : SubtargetFeature<"z180", "HasZ180Ops", "true",
                                      "Support z180 instructions">;
def FeatureEZ80    : SubtargetFeature<"ez80", "HasEZ80Ops", "true",
                                      "Support ez80 instructions">;
def FeatureIdxHalf : SubtargetFeature<"idxhalf", "HasIdxHalfRegs", "true",
                                      "Support index half registers">;
def FeatureSli     : SubtargetFeature<"sli", "HasSliOp", "true",
                                      "Support SLI instruction">;

//===----------------------------------------------------------------------===//
// ZZ80 Subtarget features
//===----------------------------------------------------------------------===//

def FeatureHWMul       : SubtargetFeature<"hwmul", "HasHWMul", "true",
                          "Hardware 8-bit multiply (MULT/MULTU)">;
def FeatureHWMulW      : SubtargetFeature<"hwmulw", "HasHWMulW", "true",
                          "Hardware 16-bit multiply (MULTW/MULTUW)">;
def FeatureHWDiv       : SubtargetFeature<"hwdiv", "HasHWDiv", "true",
                          "Hardware 8-bit divide (DIV/DIVU)">;
def FeatureHWDivW      : SubtargetFeature<"hwdivw", "HasHWDivW", "true",
                          "Hardware 16-bit divide (DIVW/DIVUW)">;
def FeatureSPRelative  : SubtargetFeature<"sp-relative", "HasSPRelative",
                          "true",
                          "SP-relative addressing mode (SP+disp16)">;
def FeaturePCRelative  : SubtargetFeature<"pc-relative", "HasPCRelative",
                          "true",
                          "PC-relative addressing mode (PC+disp16)">;
def FeatureBaseIndex   : SubtargetFeature<"base-index", "HasBaseIndex",
                          "true",
                          "Base+index addressing (HL+IX, HL+IY, IX+IY)">;
def Feature16BitALU    : SubtargetFeature<"alu16", "Has16BitALU", "true",
                          "Extended 16-bit ALU (ADDw, SUBw, CPw, NEGw)">;
def FeatureEXTS        : SubtargetFeature<"exts", "HasEXTS", "true",
                          "Sign extension instructions (EXTS, EXTSw)">;
def FeatureLDA         : SubtargetFeature<"lda", "HasLDA", "true",
                          "Load address instruction (LDA)">;
def FeatureEXReg       : SubtargetFeature<"ex-reg", "HasEXReg", "true",
                          "Extended register exchange (EX A,r / EX H,L)">;
def FeatureCoroutine   : SubtargetFeature<"coroutine", "HasCoroutine", "true",
                          "Hardware coroutine support (RAISE/LDPC)">;
def FeatureLDw         : SubtargetFeature<"ldw", "HasLDw", "true",
                          "16-bit indexed load/store (LDw)">;

//===----------------------------------------------------------------------===//
// Z80 Subtarget state
//===----------------------------------------------------------------------===//

def Mode24Bit : SubtargetFeature<"24bit-mode", "In24BitMode", "true",
                                  "24-bit mode (ez80)", [FeatureEZ80]>;
def Mode16Bit : SubtargetFeature<"16bit-mode", "In16BitMode", "true",
                                  "16-bit mode (z80)">;

//===----------------------------------------------------------------------===//
// Z80 processors supported
//===----------------------------------------------------------------------===//

let CompleteModel = 0 in def GenericModel : SchedMachineModel;
class Proc<string Name, list<SubtargetFeature> Features>
  : ProcessorModel<Name, GenericModel, Features>;
def : Proc<"generic",     []>;
def : Proc<"z80",         [FeatureUndoc, FeatureIdxHalf, FeatureSli]>;
def : Proc<"z180",        [FeatureZ180]>;
def : Proc<"ez80",        [FeatureZ180, FeatureEZ80, FeatureIdxHalf]>;
def : Proc<"zz80",        [FeatureUndoc, FeatureIdxHalf,
                            FeatureHWMul, FeatureHWMulW,
                            FeatureHWDiv, FeatureHWDivW,
                            FeatureSPRelative, FeaturePCRelative,
                            FeatureBaseIndex,
                            Feature16BitALU, FeatureEXTS, FeatureLDA,
                            FeatureEXReg, FeatureCoroutine,
                            FeatureLDw]>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "Z80RegisterInfo.td"
include "Z80RegisterBanks.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "Z80InstrInfo.td"
include "Z80Combine.td"

def Z80InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Calling Conventions
//===----------------------------------------------------------------------===//

include "Z80CallingConv.td"

//===----------------------------------------------------------------------===//
// Assembly writer
//===----------------------------------------------------------------------===//

let ShouldEmitMatchRegisterName = 0 in {
  def Z80AsmParser : AsmParser;
  def EZ80AsmParser : AsmParser;
}
def Z80AsmWriter : AsmWriter;
def EZ80AsmWriter : AsmWriter {
    string AsmWriterClassName = "EInstPrinter";
    let Variant = 1;
}

def Z80 : Target {
  // Information about the instructions...
  let InstructionSet = Z80InstrInfo;
  let AssemblyParsers = [Z80AsmParser, EZ80AsmParser];
  let AssemblyWriters = [Z80AsmWriter, EZ80AsmWriter];
}
