//===-- VariableZig.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VariableZig.h"

#include "ValueObjectVariableZig.h"
#include "ValueObjectZig.h"

using namespace lldb;
using namespace lldb_private;

VariableZig::VariableZig(user_id_t uid, const char *name, const char *mangled,
                         const SymbolFileTypeSP &symfile_type_sp,
                         ValueType scope, SymbolContextScope *owner_scope,
                         const RangeList &scope_range, Declaration *decl,
                         const DWARFExpressionList &location,
                         ZigValue *comptime_value, bool external,
                         bool artificial, bool location_is_constant_data,
                         bool static_member)
    : Variable(uid, name, mangled, symfile_type_sp, scope, owner_scope,
               scope_range, decl, location, external, artificial,
               location_is_constant_data, static_member),
      m_comptime_value(comptime_value) {}

ValueObjectSP VariableZig::CreateValueObject(ExecutionContextScope *exe_scope) {
  if (m_comptime_value)
    return ValueObjectZig::Create(exe_scope, m_comptime_value);
  return ValueObjectVariableZig::Create(exe_scope, shared_from_this());
}
