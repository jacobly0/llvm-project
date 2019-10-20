//===- EZ80InstPrinter.cpp - Convert EZ80 MCInst to assembly ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints an EZ80 MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#include "EZ80InstPrinter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "EZ80GenAsmWriter.inc"
