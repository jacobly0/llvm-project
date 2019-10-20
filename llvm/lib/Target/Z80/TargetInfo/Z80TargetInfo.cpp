//===-- Z80TargetInfo.cpp - Z80 Target Implementation ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/Z80TargetInfo.h"
#include "llvm/Support/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheZ80Target() {
  static Target TheZ80Target;
  return TheZ80Target;
}
Target &llvm::getTheEZ80Target() {
  static Target TheEZ80Target;
  return TheEZ80Target;
}

extern "C" void LLVMInitializeZ80TargetInfo() {
  RegisterTarget<Triple::z80, /*HasJIT=*/false> X(
      getTheZ80Target(), "z80", "Z80 [experimental]", "Z80");

  RegisterTarget<Triple::ez80, /*HasJIT=*/false> Y(
      getTheEZ80Target(), "ez80", "eZ80 [experimental]", "EZ80");
}
