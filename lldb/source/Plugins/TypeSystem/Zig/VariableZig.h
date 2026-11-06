//===-- ZigVariable.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VARIABLEZIG_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VARIABLEZIG_H

#include "lldb/Symbol/Variable.h"

namespace lldb_private {

class ZigValue;

class VariableZig : public Variable {
public:
  VariableZig(lldb::user_id_t uid, const char *name, const char *mangled,
              const lldb::SymbolFileTypeSP &symfile_type_sp,
              lldb::ValueType scope, SymbolContextScope *owner_scope,
              const RangeList &scope_range, Declaration *decl,
              const DWARFExpressionList &location, ZigValue *comptime_value,
              bool external, bool artificial, bool location_is_constant_data,
              bool static_member);

  ~VariableZig() override = default;

  lldb::ValueObjectSP
  CreateValueObject(ExecutionContextScope *exe_scope) override;

private:
  ZigValue *m_comptime_value;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VARIABLEZIG_H
