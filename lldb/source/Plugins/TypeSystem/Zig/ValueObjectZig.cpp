//===-- ValueObjectZig.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ValueObjectZig.h"

#include "TypeSystemZig.h"

#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectVariable.h"

#include "llvm/ADT/SmallVector.h"

using namespace lldb;
using namespace lldb_private;

ValueObjectSP ValueObjectZig::Create(ExecutionContextScope *exe_scope,
                                     CompilerType type) {
  auto type_system = type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
  if (!type_system)
    return nullptr;
  return Create(exe_scope, type_system->UnwrapType(type));
}

ValueObjectSP ValueObjectZig::Create(ExecutionContextScope *exe_scope,
                                     ZigValue *zig_value) {
  if (!zig_value)
    return nullptr;
  auto manager_sp = ValueObjectManager::Create();
  return (new ValueObjectZig(exe_scope, *manager_sp,
                             zig_value->GetTypeSystem()->weak_from_this(),
                             zig_value))
      ->GetSP();
}

std::optional<uint64_t> ValueObjectZig::GetByteSize() {
  if (auto [type_system, zig_value] = GetZigValue(); zig_value)
    return zig_value->GetType()->GetByteSize();
  return std::nullopt;
}
std::optional<uint64_t> ValueObjectZig::GetBitSize() {
  if (auto [type_system, zig_value] = GetZigValue(); zig_value)
    return zig_value->GetType()->GetBitSize();
  return std::nullopt;
}

ValueType ValueObjectZig::GetValueType() const {
  if (auto [type_system, zig_value] = GetZigValue(); zig_value)
    return llvm::isa<ZigVariable>(zig_value) ? eValueTypeVariableStatic
                                             : eValueTypeConstResult;
  return eValueTypeInvalid;
}

CompilerType ValueObjectZig::GetValueAsCompilerType() {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (ZigType *zig_type = llvm::dyn_cast_if_present<ZigType>(zig_value))
    return type_system->WrapType(zig_type);
  return type_system->WrapType();
}

ValueObjectSP ValueObjectZig::GetChildAtIndex(uint32_t idx, bool can_create) {
  ValueObjectSP child_sp = ValueObject::GetChildAtIndex(idx, can_create);
  if (ValueObjectZig *zig_child = dyn_cast_if_present(child_sp.get()))
    if (ValueObjectSP substitute_child_sp = zig_child->Substitute())
      return substitute_child_sp;
  return child_sp;
}

ValueObjectSP ValueObjectZig::GetChildMemberWithName(llvm::StringRef name,
                                                     bool can_create) {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (!zig_value)
    return nullptr;
  if (ZigScope *zig_scope = zig_value->GetNamespace()) {
    if (SymbolFile *symbol_file = type_system->GetSymbolFile())
      symbol_file->ParseDeclsForContext(type_system->WrapScope(zig_scope));
    uint32_t idx = 0;
    for (ZigDeclaration *zig_decl = zig_scope->GetFirstDecl(); zig_decl;
         zig_decl = zig_decl->GetSibling(), ++idx)
      if (zig_decl->GetName() == name)
        return GetChildAtIndex(idx, can_create);
    return nullptr;
  }
  return ValueObject::GetChildMemberWithName(name, can_create);
}

ValueObjectSP ValueObjectZig::Dereference(Status &error) {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (ZigPointer *zig_pointer =
          llvm::dyn_cast_if_present<ZigPointer>(zig_value))
    switch (zig_pointer->GetType()->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::C:
      if (zig_pointer->GetOffset() == 0 &&
          zig_pointer->GetType()->GetChildType() ==
              zig_pointer->GetPointee()->GetType()) {
        error.Clear();
        return Create(ExecutionContext(GetExecutionContextRef())
                          .GetBestExecutionContextScope(),
                      zig_pointer->GetPointee());
      }
      break;
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::Slice:
      break;
    }
  return ValueObject::Dereference(error);
}

ValueObjectSP ValueObjectZig::Clone(ConstString new_name) {
  auto [type_system, zig_value] = GetZigValue();
  ValueObjectSP new_valobj = Create(
      ExecutionContext(GetExecutionContextRef()).GetBestExecutionContextScope(),
      zig_value);
  if (new_valobj)
    new_valobj->SetName(new_name);
  return new_valobj;
}

ValueObjectSP ValueObjectZig::AddressOf(Status &error) {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (!zig_value) {
    error = Status::FromErrorString("stale object");
    return nullptr;
  }
  assert(!llvm::isa<ZigVariable>(zig_value) && "unimplemented");
  ZigPointerType *pointer_type = llvm::cast_if_present<ZigPointerType>(
      type_system->UnwrapType(type_system->GetPointerType(
          ZigPointerType::Size::One, nullptr, false, std::nullopt,
          ZigPointerType::AddressSpace::Generic, true, false,
          zig_value->GetType()->AsOpaqueType())));
  if (!pointer_type) {
    error = Status::FromErrorString("unable to create pointer type");
    return nullptr;
  }
  error.Clear();
  return Create(
      ExecutionContext(GetExecutionContextRef()).GetBestExecutionContextScope(),
      type_system->GetPointer(pointer_type, zig_value, 0));
}

ValueObjectSP ValueObjectZig::Cast(const CompilerType &compiler_type) {
  auto [old_type_system, old_value] = UnwrapZigValue();
  auto new_type_system =
      compiler_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
  if (!old_value || !new_type_system)
    return nullptr;
  ZigType *new_type = new_type_system->UnwrapType(compiler_type);
  if (!new_type)
    return nullptr;
  if (old_value->GetType() == new_type)
    return GetSP();
  ExecutionContext exe_ctx(GetExecutionContextRef());
  ExecutionContextScope *exe_scope = exe_ctx.GetBestExecutionContextScope();
  if (ZigComptimeInt *old_comptime_int =
          llvm::dyn_cast<ZigComptimeInt>(old_value)) {
    if (ZigIntType *new_int_type = llvm::dyn_cast<ZigIntType>(new_type)) {
      llvm::APSInt old_int_val = old_comptime_int->GetValue();
      if (!new_int_type->Fits(old_int_val))
        return ValueObjectConstResult::Create(
            exe_scope, Status::FromErrorStringWithFormatv(
                           "type '{}' cannot represent integer value '{}'",
                           new_type->GetQualifiedName(), old_int_val));
      return Create(exe_scope,
                    new_type_system->GetInt(
                        new_int_type,
                        old_int_val.extOrTrunc(new_int_type->GetBitSize())));
    }
  } else if (ZigComptimeFloat *old_comptime_float =
                 llvm::dyn_cast<ZigComptimeFloat>(old_value)) {
    if (ZigIntType *new_int_type = llvm::dyn_cast<ZigIntType>(new_type)) {
      llvm::APFloat old_float_val = old_comptime_float->GetValue();
      llvm::APSInt new_int_val(new_int_type->GetBitSize(),
                               !new_int_type->IsSigned());
      bool is_exact;
      if (old_float_val.convertToInteger(new_int_val,
                                         llvm::RoundingMode::NearestTiesToEven,
                                         &is_exact) != llvm::APFloat::opOK)
        return ValueObjectConstResult::Create(
            exe_scope, Status::FromErrorStringWithFormatv(
                           "type '{}' cannot represent float value '{}'",
                           new_type->GetQualifiedName(), old_float_val));
      return Create(exe_scope,
                    new_type_system->GetInt(new_int_type, new_int_val));
    } else if (ZigFloatType *new_float_type =
                   llvm::dyn_cast<ZigFloatType>(new_type)) {
      llvm::APFloat float_val = old_comptime_float->GetValue();
      bool loses_info;
      float_val.convert(new_float_type->GetSemantics(),
                        llvm::RoundingMode::NearestTiesToEven, &loses_info);
      return Create(exe_scope,
                    new_type_system->GetFloat(new_float_type, float_val));
    }
  } else if (ZigInt *old_int = llvm::dyn_cast<ZigInt>(old_value)) {
    if (llvm::isa<ZigComptimeIntType>(new_type))
      return Create(exe_scope,
                    new_type_system->GetComptimeInt(old_int->GetValue()));
    if (ZigIntType *new_int_type = llvm::dyn_cast<ZigIntType>(new_type)) {
      llvm::APSInt old_int_val = old_int->GetValue();
      if (!new_int_type->Fits(old_int_val))
        return ValueObjectConstResult::Create(
            exe_scope, Status::FromErrorStringWithFormatv(
                           "type '{}' cannot represent integer value '{}'",
                           new_type->GetQualifiedName(), old_int_val));
      return Create(exe_scope,
                    new_type_system->GetInt(
                        new_int_type,
                        old_int_val.extOrTrunc(new_int_type->GetBitSize())));
    }
  } else if (llvm::isa<ZigNullType>(old_value->GetType())) {
    if (ZigPointerType *new_ptr_type = llvm::dyn_cast<ZigPointerType>(new_type))
      if (new_ptr_type->GetSize() == ZigPointerType::Size::C)
        return Create(
            exe_scope,
            new_type_system->GetData(
                new_ptr_type, llvm::SmallVector<uint8_t, 8>(
                                  new_type_system->GetPointerByteSize(), 0)));
  } else if (ZigPointer *old_pointer = llvm::dyn_cast<ZigPointer>(old_value);
             old_pointer && !llvm::isa<ZigSlice>(old_pointer)) {
    if (ZigPointerType *new_pointer_type =
            llvm::dyn_cast<ZigPointerType>(new_type);
        new_pointer_type &&
        new_pointer_type->GetSize() != ZigPointerType::Size::Slice) {
      return Create(exe_scope, new_type_system->GetPointer(
                                   new_pointer_type, old_pointer->GetPointee(),
                                   old_pointer->GetOffset()));
    }
  }
  return ValueObjectConstResult::Create(
      exe_scope, Status::FromErrorStringWithFormatv(
                     "unimplemented cast from '{}' to '{}'",
                     old_value->GetType()->GetQualifiedName(),
                     new_type->GetQualifiedName()));
}

ValueObjectSP ValueObjectZig::CastPointerType(const char *name,
                                              CompilerType &compiler_type) {
  if (compiler_type.IsPointerType())
    if (ValueObjectSP result = Cast(compiler_type)) {
      result->SetName(ConstString(name));
      return result;
    }
  return ValueObject::CastPointerType(name, compiler_type);
}

ValueObjectSP ValueObjectZig::CastPointerType(const char *name,
                                              TypeSP &type_sp) {
  if (type_sp)
    if (CompilerType compiler_type = type_sp->GetForwardCompilerType();
        compiler_type.IsPointerType())
      if (ValueObjectSP result = Cast(compiler_type);
          result && result->GetError().Success()) {
        result->SetName(ConstString(name));
        return result;
      }
  return ValueObject::CastPointerType(name, type_sp);
}

bool ValueObjectZig::MightHaveChildren() {
  if (auto [type_system, zig_value] = UnwrapZigValue();
      llvm::isa_and_present<ZigType>(zig_value))
    return zig_value->GetNamespace();
  return ValueObject::MightHaveChildren();
}

auto ValueObjectZig::GetZigValue() const
    -> std::pair<TypeSystemZigSP, ZigValue *> {
  TypeSystemZigSP type_system =
      CompilerType::TypeSystemSPWrapper(m_type_system.lock())
          .dyn_cast_if_present<TypeSystemZig>();
  return std::make_pair(std::move(type_system),
                        type_system ? m_zig_value : nullptr);
}
auto ValueObjectZig::UnwrapZigValue() const
    -> std::pair<TypeSystemZigSP, ZigValue *> {
  auto [type_system, zig_value] = GetZigValue();
  while (ZigConstant *zig_constant =
             llvm::dyn_cast_if_present<ZigConstant>(zig_value))
    zig_value = zig_constant->GetValue();
  return std::make_pair(std::move(type_system), zig_value);
}
ValueObjectSP ValueObjectZig::Unwrap() {
  auto [type_system, zig_value] = GetZigValue();
  if (ZigConstant *zig_constant =
          llvm::dyn_cast_if_present<ZigConstant>(zig_value))
    return (new ValueObjectZig(*this, type_system, zig_constant->GetValue()))
        ->GetSP();
  return GetSP();
}

ValueObjectSP ValueObjectZig::Substitute() {
  if (m_substitute)
    return m_substitute;
  auto [type_system, zig_value] = UnwrapZigValue();
  switch (zig_value->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
    llvm_unreachable("already unwrapped");
  case ZigValue::Kind::Variable: {
    ZigVariable *zig_variable = llvm::cast<ZigVariable>(zig_value);
    CompilerDecl var_decl = type_system->WrapDecl(zig_variable);
    if (StackFrameSP frame = GetFrameSP()) {
      VariableListSP vars = frame->GetInScopeVariableList(true);
      for (VariableSP var : *vars)
        if (var->GetDecl() == var_decl) {
          m_substitute = ValueObjectVariable::Create(frame.get(), var);
          m_substitute->SetName(zig_variable->GetName());
          break;
        }
    }
    break;
  }
  case ZigValue::Kind::Function:
  case ZigValue::Kind::OnlyPossibleValue:
  case ZigValue::Kind::ComptimeInt:
  case ZigValue::Kind::ComptimeFloat:
  case ZigValue::Kind::EnumLiteral:
  case ZigValue::Kind::Pointer:
  case ZigValue::Kind::Slice:
  case ZigValue::Kind::TypeType:
  case ZigValue::Kind::VoidType:
  case ZigValue::Kind::NoReturnType:
  case ZigValue::Kind::ComptimeIntType:
  case ZigValue::Kind::ComptimeFloatType:
  case ZigValue::Kind::UndefinedType:
  case ZigValue::Kind::NullType:
  case ZigValue::Kind::AnyOpaqueType:
  case ZigValue::Kind::EnumLiteralType:
  case ZigValue::Kind::AnyType:
  case ZigValue::Kind::BoolType:
  case ZigValue::Kind::IntType:
  case ZigValue::Kind::FloatType:
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    break;
  case ZigValue::Kind::Data: {
    if (zig_value->GetType()->HasComptimeState())
      break;
    llvm::ArrayRef<uint8_t> data = llvm::cast<ZigData>(zig_value)->GetData();
    m_substitute = ValueObject::CreateValueObjectFromData(
        GetName(),
        DataExtractor(data.data(), data.size(), type_system->GetByteOrder(),
                      type_system->GetPointerByteSize()),
        ExecutionContext(GetExecutionContextRef()),
        type_system->WrapType(zig_value->GetType()));
    break;
  }
  case ZigValue::Kind::BoolFalse:
  case ZigValue::Kind::BoolTrue:
    m_substitute = ValueObject::CreateValueObjectFromBool(
        GetTargetSP(), llvm::cast<ZigBool>(zig_value)->GetValue(), GetName());
    break;
  case ZigValue::Kind::Int:
    m_substitute = ValueObject::CreateValueObjectFromAPInt(
        GetTargetSP(), llvm::cast<ZigInt>(zig_value)->GetValue(),
        type_system->WrapType(zig_value->GetType()), GetName());
    break;
  case ZigValue::Kind::Float:
    m_substitute = ValueObject::CreateValueObjectFromAPFloat(
        GetTargetSP(), llvm::cast<ZigFloat>(zig_value)->GetValue(),
        type_system->WrapType(zig_value->GetType()), GetName());
    break;
  case ZigValue::Kind::Tag:
    m_substitute = ValueObject::CreateValueObjectFromAPInt(
        GetTargetSP(), llvm::cast<ZigTag>(zig_value)->GetValue()->GetValue(),
        type_system->WrapType(zig_value->GetType()), GetName());
    break;
  }
  return m_substitute;
}

bool ValueObjectZig::UpdateValue() {
  // Const value is always valid
  return true;
}

ValueObject *ValueObjectZig::CreateChildAtIndex(size_t idx) {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (!zig_value)
    return nullptr;
  if (ZigScope *zig_scope = zig_value->GetNamespace()) {
    if (SymbolFile *symbol_file = type_system->GetSymbolFile())
      symbol_file->ParseDeclsForContext(type_system->WrapScope(zig_scope));
    for (ZigDeclaration *zig_decl = zig_scope->GetFirstDecl(); zig_decl;
         zig_decl = zig_decl->GetSibling(), --idx)
      if (!idx)
        return new ValueObjectZig(*this, type_system, zig_decl);
    return nullptr;
  } else if (ZigTupleType *zig_tuple_type =
                 llvm::dyn_cast<ZigTupleType>(zig_value->GetType())) {
    llvm::ArrayRef<ZigTupleField> fields = zig_tuple_type->GetFields();
    if (idx < fields.size() && fields[idx].IsComptime()) {
      ValueObjectZig *child = new ValueObjectZig(
          *this, type_system, fields[idx].GetComptimeValue());
      child->SetName(ConstString(llvm::formatv("[{0:d}]", idx).str()));
      return child;
    }
  } else if (ZigRecordType *zig_record_type =
                 llvm::dyn_cast<ZigRecordType>(zig_value->GetType())) {
    llvm::ArrayRef<ZigRecordField> fields = zig_record_type->GetFields();
    if (idx < fields.size() && fields[idx].IsComptime()) {
      ValueObjectZig *child =
          new ValueObjectZig(*this, type_system, fields[idx].GetDefaultValue());
      child->SetName(fields[idx].GetName());
      return child;
    }
  }
  return ValueObject::CreateChildAtIndex(idx);
}

llvm::Expected<uint32_t>
ValueObjectZig::CalculateNumChildren(uint32_t max_children) {
  auto [type_system, zig_value] = UnwrapZigValue();
  if (!zig_value)
    return llvm::createStringError("missing type system");
  if (ZigScope *zig_scope = zig_value->GetNamespace()) {
    if (SymbolFile *symbol_file = type_system->GetSymbolFile())
      symbol_file->ParseDeclsForContext(type_system->WrapScope(zig_scope));
    uint32_t num_children = 0;
    for (ZigDeclaration *zig_decl = zig_scope->GetFirstDecl();
         num_children < max_children && zig_decl;
         zig_decl = zig_decl->GetSibling())
      ++num_children;
    return num_children;
  } else if (llvm::isa<ZigType>(zig_value))
    return 0;

  ExecutionContext exe_ctx(GetExecutionContextRef());
  auto num_children = GetCompilerType().GetNumChildren(true, &exe_ctx);
  if (!num_children)
    return num_children;
  return std::min(*num_children, max_children);
}

CompilerType ValueObjectZig::GetCompilerTypeImpl() {
  if (auto [type_system, zig_value] = GetZigValue(); zig_value)
    return type_system->WrapType(zig_value->GetType());
  return CompilerType();
}

ValueObjectZig::ValueObjectZig(ExecutionContextScope *exe_scope,
                               ValueObjectManager &manager,
                               TypeSystemWP type_system, ZigValue *zig_value)
    : ValueObject(exe_scope, manager), m_type_system(type_system),
      m_zig_value(zig_value) {
  Init();
}

ValueObjectZig::ValueObjectZig(ValueObject &parent, TypeSystemWP type_system,
                               ZigValue *zig_value)
    : ValueObject(parent), m_type_system(type_system), m_zig_value(zig_value) {
  Init();
}

void ValueObjectZig::Init() {
  SetIsConstant();
  SetValueIsValid(true);
  m_language_flags |= uint64_t(LanguageFlags::IsValueObjectZig);
  auto [type_system, zig_value] = GetZigValue();
  if (!zig_value)
    return;
  if (ZigDeclaration *zig_decl = llvm::dyn_cast<ZigDeclaration>(zig_value)) {
    SetName(zig_decl->GetName());
    while (ZigConstant *zig_constant = llvm::dyn_cast<ZigConstant>(zig_value))
      zig_value = zig_constant->GetValue();
  } else if (ZigType *zig_type = llvm::dyn_cast<ZigType>(zig_value))
    SetName(zig_type->GetName());
  if (!zig_value->GetType()->HasComptimeState())
    m_value.SetCompilerType(type_system->WrapType(zig_value->GetType()));
  switch (zig_value->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
  case ZigValue::Kind::Variable:
  case ZigValue::Kind::Function:
    break;
  case ZigValue::Kind::Data: {
    auto data = llvm::cast<ZigData>(zig_value)->GetData();
    m_value.SetBytes(data.data(), data.size());
    break;
  }
  case ZigValue::Kind::OnlyPossibleValue:
  case ZigValue::Kind::EnumLiteral:
    break;
  case ZigValue::Kind::ComptimeInt:
    m_value.GetScalar() = llvm::cast<ZigComptimeInt>(zig_value)->GetValue();
    break;
  case ZigValue::Kind::ComptimeFloat:
    m_value.GetScalar() = llvm::cast<ZigComptimeFloat>(zig_value)->GetValue();
    break;
  case ZigValue::Kind::BoolFalse:
  case ZigValue::Kind::BoolTrue:
    m_value.GetScalar() = llvm::cast<ZigBool>(zig_value)->GetValue();
    break;
  case ZigValue::Kind::Int:
    m_value.GetScalar() = llvm::cast<ZigInt>(zig_value)->GetValue();
    break;
  case ZigValue::Kind::Float:
    m_value.GetScalar() = llvm::cast<ZigFloat>(zig_value)->GetValue();
    break;
  case ZigValue::Kind::Pointer: {
    ZigPointer *zig_pointer = llvm::cast<ZigPointer>(zig_value);
    if (ZigData *zig_data =
            llvm::dyn_cast<ZigData>(zig_pointer->GetPointee())) {
      DataBufferSP data(new DataBufferHeap(zig_data->GetData().data(),
                                           zig_data->GetData().size()));
      m_data.SetData(data, zig_pointer->GetOffset(),
                     zig_pointer->GetType()->GetChildType()->GetByteSize());
      m_value.GetScalar() = (uintptr_t)m_data.GetDataStart();
      SetAddressTypeOfChildren(eAddressTypeHost);
    }
    break;
  }
  case ZigValue::Kind::Slice:
    break;
  case ZigValue::Kind::Tag:
    m_value.GetScalar() = llvm::cast<ZigTag>(zig_value)->GetValue()->GetValue();
    break;
  case ZigValue::Kind::TypeType:
  case ZigValue::Kind::VoidType:
  case ZigValue::Kind::NoReturnType:
  case ZigValue::Kind::ComptimeIntType:
  case ZigValue::Kind::ComptimeFloatType:
  case ZigValue::Kind::UndefinedType:
  case ZigValue::Kind::NullType:
  case ZigValue::Kind::AnyOpaqueType:
  case ZigValue::Kind::EnumLiteralType:
  case ZigValue::Kind::AnyType:
  case ZigValue::Kind::BoolType:
  case ZigValue::Kind::IntType:
  case ZigValue::Kind::FloatType:
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    break;
  }
  SetPreferredDisplayLanguage(eLanguageTypeZig);
}
