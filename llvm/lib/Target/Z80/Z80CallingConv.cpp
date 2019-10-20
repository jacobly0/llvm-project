//=== Z80CallingConv.cpp - Z80 Custom Calling Convention Impl   -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of custom routines for the Z80
// Calling Convention that aren't done by tablegen.
//
//===----------------------------------------------------------------------===//

#include "Z80CallingConv.h"
#include "Z80Subtarget.h"
using namespace llvm;

static bool CC_Z80_HandleSplit(unsigned ValNo, MVT ValVT, MVT LocVT,
                               CCValAssign::LocInfo LocInfo,
                               ISD::ArgFlagsTy ArgFlags, CCState &State);

// Provides entry points of CC_Z80 and RetCC_Z80.
#include "Z80GenCallingConv.inc"

static bool CC_Z80_HandleSplit(unsigned ValNo, MVT ValVT, MVT LocVT,
                               CCValAssign::LocInfo LocInfo,
                               ISD::ArgFlagsTy ArgFlags, CCState &State) {
  auto &PendingLocs = State.getPendingLocs();

  if (!ArgFlags.isSplit() && PendingLocs.empty())
    return false;

  if (PendingLocs.size() > ArgFlags.isSplitEnd())
    LocVT = PendingLocs.front().getLocVT();
  PendingLocs.push_back(CCValAssign::getPending(ValNo, ValVT, LocVT, LocInfo));

  if (!ArgFlags.isSplitEnd())
    return true;

  static_assert(MVT::i8 + 1 == MVT::i16 && MVT::i16 + 1 == MVT::i24,
                "Assuming enum values are consecutive");
  static const MCPhysReg RegList[][4] = {
      {Z80::L, Z80::E, Z80::C, Z80::IYL},
      {Z80::HL, Z80::DE, Z80::BC, Z80::IY},
      {Z80::UHL, Z80::UDE, Z80::UBC, Z80::UIY},
  };
  static const MCPhysReg ShadowRegList[][4] = {
      {Z80::E, Z80::E, Z80::IYL, Z80::IYL},
      {Z80::DE, Z80::DE, Z80::IY, Z80::IY},
      {Z80::UDE, Z80::UDE, Z80::UIY, Z80::UIY},
  };

  if (PendingLocs.size() == 2) {
    for (auto &PendingLoc : PendingLocs) {
      MVT PendingValVT = PendingLoc.getValVT();
      assert((PendingValVT == MVT::i8 || PendingValVT == MVT::i16 ||
              PendingValVT == MVT::i24) &&
             "Unexpected type");
      Register Reg =
          State.AllocateReg(RegList[PendingValVT.SimpleTy - MVT::i8]);
      assert(Reg.isValid() && "Ran out of registers!");
      PendingLoc.convertToReg(Reg);
      State.addLoc(PendingLoc);
    }
  } else {
    MVT PendingValVT = PendingLocs.front().getValVT();
    assert((PendingValVT == MVT::i8 || PendingValVT == MVT::i16 ||
            PendingValVT == MVT::i24) &&
           "Unexpected type");
    Register Reg =
        State.AllocateReg(RegList[PendingValVT.SimpleTy - MVT::i8],
                          ShadowRegList[PendingValVT.SimpleTy - MVT::i8]);
    assert(Reg.isValid() && "Ran out of registers!");
    for (auto &PendingLoc : PendingLocs) {
      PendingLoc.convertToReg(Reg);
      PendingLoc.setLocInfo(CCValAssign::Indirect);
      State.addLoc(PendingLoc);
    }
  }

  PendingLocs.clear();
  return true;
}
