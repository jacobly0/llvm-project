//===-- ClangPersistentVariables.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigPersistentVariables.h"
#include "ZigExpressionVariable.h"

#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "Plugins/TypeSystem/Zig/ZigValue.h"

using namespace lldb;
using namespace lldb_private;

char ZigPersistentVariables::ID;

ExpressionVariableSP ZigPersistentVariables::CreatePersistentVariable(
    const ValueObjectSP &valobj_sp) {
  return AddNewlyConstructedVariable(new ZigExpressionVariable(valobj_sp));
}

ExpressionVariableSP ZigPersistentVariables::CreatePersistentVariable(
    ExecutionContextScope *exe_scope, ConstString name,
    const CompilerType &compiler_type, ByteOrder byte_order,
    uint32_t addr_byte_size) {
  return AddNewlyConstructedVariable(new ZigExpressionVariable(
      exe_scope, name, compiler_type, byte_order, addr_byte_size));
}

ConstString
ZigPersistentVariables::GetNextPersistentVariableName(bool is_error) {
  llvm::SmallString<64> name;
  {
    llvm::raw_svector_ostream os(name);
    os << GetPersistentVariablePrefix(is_error)
       << m_next_persistent_variable_id++;
  }
  return ConstString(name);
}

void ZigPersistentVariables::RemovePersistentVariable(
    ExpressionVariableSP variable) {
  RemoveVariable(variable);

  // Check if the removed variable was the last one that was created. If yes,
  // reuse the variable id for the next variable.

  // Nothing to do if we have not assigned a variable id so far.
  if (m_next_persistent_variable_id == 0)
    return;

  llvm::StringRef name = variable->GetName().GetStringRef();
  // Remove the prefix from the variable that only the index is left.
  if (!name.consume_front(GetPersistentVariablePrefix()))
    return;

  // Check if the variable contained a variable id.
  uint32_t variable_id;
  if (name.getAsInteger(10, variable_id))
    return;
  // If it's the most recent variable id that was assigned, make sure that this
  // variable id will be used for the next persistent variable.
  if (variable_id == m_next_persistent_variable_id - 1)
    m_next_persistent_variable_id--;
}

std::optional<CompilerType>
ZigPersistentVariables::GetCompilerTypeFromPersistentDecl(
    ConstString type_name) {
  if (auto var_sp = GetVariable(type_name))
    if (auto valobj_sp = var_sp->GetValueObject())
      if (CompilerType compiler_type = valobj_sp->GetValueAsCompilerType())
        return compiler_type;
  return std::nullopt;
}
