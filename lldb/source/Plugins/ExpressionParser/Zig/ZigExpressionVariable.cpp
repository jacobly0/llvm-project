//===-- ZigExpressionVariable.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigExpressionVariable.h"

#include "lldb/ValueObject/ValueObjectConstResult.h"

using namespace lldb;
using namespace lldb_private;

char ZigExpressionVariable::ID;

ZigExpressionVariable::ZigExpressionVariable(const ValueObjectSP &valobj_sp) {
  m_frozen_sp = valobj_sp;
}

ZigExpressionVariable::ZigExpressionVariable(ExecutionContextScope *exe_scope,
                                             ConstString name,
                                             const TypeFromUser &user_type,
                                             ByteOrder byte_order,
                                             uint32_t addr_byte_size) {
  m_flags = EVNone;
  m_frozen_sp =
      ValueObjectConstResult::Create(exe_scope, byte_order, addr_byte_size);
  SetName(name);
  SetCompilerType(user_type);
}
