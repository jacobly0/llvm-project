//===-- ZigExpressionVariable.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGEXPRESSIONVARIABLE_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGEXPRESSIONVARIABLE_H

#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/Expression/ExpressionVariable.h"

namespace lldb_private {

class ZigExpressionVariable
    : public llvm::RTTIExtends<ZigExpressionVariable, ExpressionVariable> {
public:
  // LLVM RTTI support
  static char ID;

  ZigExpressionVariable(const lldb::ValueObjectSP &valobj_sp);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGEXPRESSIONVARIABLE_H
