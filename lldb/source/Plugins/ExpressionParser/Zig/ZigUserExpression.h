//===-- ZigUserExpression.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGUSEREXPRESSION_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGUSEREXPRESSION_H

#include "lldb/Expression/UserExpression.h"

namespace lldb_private {

class ZigUserExpression : public UserExpression {
  // LLVM RTTI support
  static char ID;

public:
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || UserExpression::isA(ClassID);
  }
  static bool classof(const Expression *obj) { return obj->isA(&ID); }

  ZigUserExpression(ExecutionContextScope &exe_scope, llvm::StringRef expr,
                    llvm::StringRef prefix, SourceLanguage language,
                    ResultType desired_type,
                    const EvaluateExpressionOptions &options,
                    ValueObject *ctx_obj);

  ~ZigUserExpression() override = default;

  bool Parse(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
             lldb_private::ExecutionPolicy execution_policy,
             bool keep_result_in_memory, bool generate_debug_info) override;

  bool CanInterpret() override { return true; }

  bool FinalizeJITExecution(DiagnosticManager &diagnostic_manager,
                            ExecutionContext &exe_ctx,
                            lldb::ExpressionVariableSP &result,
                            lldb::addr_t function_stack_bottom,
                            lldb::addr_t function_stack_top) override {
    llvm_unreachable("unimplemented");
  }

protected:
  lldb::ExpressionResults
  DoExecute(DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
            const EvaluateExpressionOptions &options,
            lldb::UserExpressionSP &shared_ptr_to_me,
            lldb::ExpressionVariableSP &result) override;

private:
  /// The object (if any) in which context the expression is evaluated.
  /// Can be referred to with `@this()` inside the expression.
  /// See the comment to `UserExpression::Evaluate` for details.
  ValueObject *m_ctx_obj;

  lldb::ValueObjectSP m_result;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGUSEREXPRESSION_H
