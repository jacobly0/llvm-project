//===-- ValueObjectVariableZig.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTVARIABLEZIG_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTVARIABLEZIG_H

#include "lldb/ValueObject/ValueObjectVariable.h"

namespace lldb_private {

class ValueObjectVariableZig : public ValueObjectVariable {
public:
  ~ValueObjectVariableZig() override = default;

private:
  friend class VariableZig; // For Create

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    const lldb::VariableSP &var_sp);

  using ValueObjectVariable::ValueObjectVariable;
  // For ValueObject only
  ValueObjectVariableZig(const ValueObjectVariable &) = delete;
  const ValueObjectVariableZig &
  operator=(const ValueObjectVariableZig &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTVARIABLEZIG_H
