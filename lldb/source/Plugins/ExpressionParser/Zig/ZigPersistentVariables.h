//===-- ZigPersistentVariables.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGPERSISTENTVARIABLES_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGPERSISTENTVARIABLES_H

#include "lldb/Expression/ExpressionVariable.h"

namespace lldb_private {

class ZigPersistentVariables
    : public llvm::RTTIExtends<ZigPersistentVariables,
                               PersistentExpressionState> {
public:
  // LLVM RTTI support
  static char ID;

  ~ZigPersistentVariables() override = default;

  lldb::ExpressionVariableSP
  CreatePersistentVariable(const lldb::ValueObjectSP &valobj_sp) override;

  lldb::ExpressionVariableSP
  CreatePersistentVariable(ExecutionContextScope *exe_scope, ConstString name,
                           const CompilerType &compiler_type,
                           lldb::ByteOrder byte_order,
                           uint32_t addr_byte_size) override;

  ConstString GetNextPersistentVariableName(bool is_error = false) override;

  void RemovePersistentVariable(lldb::ExpressionVariableSP variable) override;

  std::optional<CompilerType>
  GetCompilerTypeFromPersistentDecl(ConstString type_name) override;

protected:
  llvm::StringRef
  GetPersistentVariablePrefix(bool is_error = false) const override {
    return "$";
  }

private:
  // The counter used by GetNextPersistentVariableName
  uint32_t m_next_persistent_variable_id = 0;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGPERSISTENTVARIABLES_H
