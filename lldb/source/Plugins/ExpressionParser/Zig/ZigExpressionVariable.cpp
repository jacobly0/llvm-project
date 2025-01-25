//===-- ZigExpressionVariable.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigExpressionVariable.h"

using namespace lldb_private;

char ZigExpressionVariable::ID;

ZigExpressionVariable::ZigExpressionVariable(
    const lldb::ValueObjectSP &valobj_sp) {
  m_frozen_sp = valobj_sp;
}
