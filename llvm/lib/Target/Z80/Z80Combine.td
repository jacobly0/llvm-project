//=- Z80.td - Define Z80 Combine Rules -----------------------*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

include "llvm/Target/GlobalISel/Combine.td"

def Z80PreLegalizerCombinerHelper: GICombinerHelper<
  "Z80GenPreLegalizerCombinerHelper", [lower_is_power_of_two]> {
  let DisableRuleOption = "z80prelegalizercombiner-disable-rule";
}
