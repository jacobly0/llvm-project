//===-- ValueObjectZig.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTZIG_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTZIG_H

#include "lldb/ValueObject/ValueObject.h"

#include <memory>
#include <utility>

namespace lldb_private {

class TypeSystemZig;
class ZigValue;

class ValueObjectZig : public ValueObject {
  using TypeSystemZigSP = std::shared_ptr<TypeSystemZig>;

public:
  enum class LanguageFlags : uint64_t {
    IsValueObjectZig = 1u << 0,
  };

  ~ValueObjectZig() override = default;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    CompilerType type);
  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    ZigValue *zig_value);

  std::optional<uint64_t> GetByteSize() override;
  std::optional<uint64_t> GetBitSize() override;

  lldb::ValueType GetValueType() const override;

  CompilerType GetValueAsCompilerType() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx, bool can_create) override;

  lldb::ValueObjectSP GetChildMemberWithName(llvm::StringRef name,
                                             bool can_create) override;

  lldb::ValueObjectSP Dereference(Status &error) override;

  lldb::ValueObjectSP Clone(ConstString new_name) override;

  lldb::ValueObjectSP AddressOf(Status &error) override;

  lldb::ValueObjectSP Cast(const CompilerType &compiler_type) override;

  lldb::ValueObjectSP CastPointerType(const char *name,
                                      CompilerType &ast_type) override;

  lldb::ValueObjectSP CastPointerType(const char *name,
                                      lldb::TypeSP &type_sp) override;

  bool MightHaveChildren() override;

  std::pair<TypeSystemZigSP, ZigValue *> GetZigValue() const;
  std::pair<TypeSystemZigSP, ZigValue *> UnwrapZigValue() const;
  lldb::ValueObjectSP Unwrap();

  static ValueObjectZig *dyn_cast(ValueObject *valobj) {
    valobj = valobj->GetNonSyntheticValue().get();
    if (valobj->IsDynamic())
      return nullptr;
    if (!valobj->GetIsConstant())
      return nullptr;
    if (valobj->GetValueType() != lldb::eValueTypeVariableStatic &&
        valobj->GetValueType() != lldb::eValueTypeConstResult)
      return nullptr;
    if (valobj->GetPreferredDisplayLanguage() != lldb::eLanguageTypeZig)
      return nullptr;
    if (!(valobj->GetLanguageFlags() &
          uint64_t(LanguageFlags::IsValueObjectZig)))
      return nullptr;
    return static_cast<ValueObjectZig *>(valobj);
  }
  static ValueObjectZig *dyn_cast_if_present(ValueObject *valobj) {
    return valobj ? dyn_cast(valobj) : nullptr;
  }

  lldb::ValueObjectSP Substitute();

protected:
  bool UpdateValue() override;

  ValueObject *CreateChildAtIndex(size_t idx) override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  CompilerType GetCompilerTypeImpl() override;

private:
  ValueObjectZig(ExecutionContextScope *exe_scope, ValueObjectManager &manager,
                 lldb::TypeSystemWP type_system, ZigValue *zig_value);

  ValueObjectZig(ValueObject &parent, lldb::TypeSystemWP type_system,
                 ZigValue *zig_value);

  void Init();

  lldb::TypeSystemWP m_type_system;
  ZigValue *m_zig_value;
  lldb::ValueObjectSP m_substitute;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_VALUEOBJECTZIG_H
