//===-- Z80FixupKinds.h - Z80 Specific Fixup Entries ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_Z80_FIXUP_KINDS_H
#define LLVM_Z80_FIXUP_KINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace Z80 {

/// The set of supported fixups.
///
/// Although most of the current fixup types reflect a unique relocation
/// one can have multiple fixup types for a given relocation and thus need
/// to be uniquely named.
///
/// \note This table *must* be in the same order of
///       MCFixupKindInfo Infos[Z80::NumTargetFixupKinds]
///       in `Z80AsmBackend.cpp`.
enum Fixups {
  fixup_8 = FirstTargetFixupKind,
  fixup_8_dis,
  fixup_8_pcrel,
  fixup_16,
  fixup_24,
  fixup_32,
  fixup_byte0,
  fixup_byte1,
  fixup_byte2,
  fixup_byte3,
  fixup_word0,
  fixup_word1,
  fixup_16_be,
  // Marker
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end of namespace Z80
} // end of namespace llvm

#endif // LLVM_Z80_FIXUP_KINDS_H
