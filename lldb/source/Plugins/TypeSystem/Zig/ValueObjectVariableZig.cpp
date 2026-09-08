//===-- ValueObjectVariableZig.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ValueObjectVariableZig.h"

using namespace lldb;
using namespace lldb_private;

lldb::ValueObjectSP
ValueObjectVariableZig::Create(ExecutionContextScope *exe_scope,
                               const lldb::VariableSP &var_sp) {
  auto manager_sp = ValueObjectManager::Create();
  return (new ValueObjectVariableZig(exe_scope, *manager_sp, var_sp))->GetSP();
}
