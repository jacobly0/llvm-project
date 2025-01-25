//===-- ZigValue.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigValue.h"

#include "TypeSystemZig.h"

#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"

#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>

using namespace lldb_private;

ZigScope::ZigScope(TypeSystemZig *type_system, Kind kind)
    : m_kind(kind), m_first_decl(nullptr), m_last_decl(nullptr) {}

void ZigScope::UpdateQualifiedName() const {
  StreamString s;
  if (ZigScope *parent = GetParent())
    s << parent->GetQualifiedName() << '.';
  s << GetName();
  assert(m_qualified_name.IsNull() && "qualified name already updated");
  m_qualified_name = ConstString(s.GetString());
}

ZigModule::ZigModule(TypeSystemZig *type_system, ConstString name)
    : ZigScope(type_system, Kind::Module), m_type_system(type_system),
      m_name(name) {}

ZigValue::ZigValue(TypeSystemZig *type_system, Kind kind, ZigType *type)
    : m_kind(kind), m_mark(false), m_type(type) {}

ZigDeclaration::ZigDeclaration(TypeSystemZig *type_system, Kind kind,
                               ZigScope *parent, ConstString name,
                               ZigType *type)
    : ZigValue(type_system, kind, type), m_parent(parent), m_sibling(nullptr),
      m_name(name) {
  assert(classof(this) && "invalid kind");

  if (ZigDeclaration *prev_decl = std::exchange(parent->m_last_decl, this))
    prev_decl->m_sibling = this;
  else
    parent->m_first_decl = this;
}

ZigConstant::ZigConstant(TypeSystemZig *type_system, ZigScope *parent,
                         ConstString name, ZigValue *value)
    : ZigConstant(type_system, Kind::Constant, parent, name, value) {}

ZigConstant::ZigConstant(TypeSystemZig *type_system, Kind kind,
                         ZigScope *parent, ConstString name, ZigValue *value)
    : ZigDeclaration(type_system, kind, parent, name, value->GetType()),
      m_value(value) {
  assert(classof(this) && "invalid kind");
}

ZigAlias::ZigAlias(TypeSystemZig *type_system, ZigScope *parent,
                   ConstString name, ZigType *value)
    : ZigConstant(type_system, Kind::Alias, parent, name, value) {}

ZigVariable::ZigVariable(TypeSystemZig *type_system, ZigScope *parent,
                         ConstString name, ZigType *type)
    : ZigDeclaration(type_system, Kind::Variable, parent, name, type) {}

ZigFunction::ZigFunction(TypeSystemZig *type_system, ZigScope *parent,
                         ConstString name, ZigFunctionType *type)
    : ZigScope(type_system, ZigScope::Kind::Function),
      ZigDeclaration(type_system, ZigValue::Kind::Function, parent, name,
                     type) {}

ZigBlock::ZigBlock(TypeSystemZig *type_system, ZigScope *parent)
    : ZigScope(type_system, ZigScope::Kind::Block), m_parent(parent) {}

ZigType::ZigType(TypeSystemZig *type_system, Kind kind, ZigTypeType *type,
                 uint64_t byte_size, llvm::Align align)
    : ZigValue(type_system, kind, type), m_byte_size(byte_size),
      m_align(align) {
  assert(classof(this) && "invalid kind");
}

ZigType::ZigType(TypeSystemZig *type_system, Kind kind, uint64_t byte_size,
                 llvm::Align align)
    : ZigType(type_system, kind,
              llvm::cast<ZigTypeType>(
                  type_system->UnwrapType(type_system->GetTypeType())),
              byte_size, align) {}

void ZigType::UpdateName() const {
  StreamString s;
  PrintName(s);
  assert(m_name.IsNull() && "name already updated");
  m_name = ConstString(s.GetString());
}

ZigData::ZigData(TypeSystemZig *type_system, ZigType *type,
                 llvm::ArrayRef<uint8_t> data)
    : ZigValue(type_system, Kind::Data, type) {
  assert(type->GetByteSize() == data.size() && "type and data size mismatch");
  std::uninitialized_copy(data.begin(), data.end(),
                          getTrailingObjects<uint8_t>());
}

ZigOnlyPossibleValue::ZigOnlyPossibleValue(TypeSystemZig *type_system,
                                           ZigType *type)
    : ZigValue(type_system, Kind::OnlyPossibleValue, type) {}

ZigTypeType::ZigTypeType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::TypeType, this, 0, llvm::Align()),
      m_type_system(type_system) {}

void ZigTypeType::PrintName(Stream &s) const { s << "type"; }

ZigVoidType::ZigVoidType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::VoidType, 0, llvm::Align()) {}

void ZigVoidType::PrintName(Stream &s) const { s << "void"; }

ZigNoReturnType::ZigNoReturnType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::NoReturnType, 0, llvm::Align()) {}

void ZigNoReturnType::PrintName(Stream &s) const { s << "noreturn"; }

ZigComptimeIntType::ZigComptimeIntType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::ComptimeIntType, 0, llvm::Align()) {}

void ZigComptimeIntType::PrintName(Stream &s) const { s << "comptime_int"; }

ZigComptimeInt::ZigComptimeInt(TypeSystemZig *type_system,
                               const llvm::APSInt &value)
    : ZigValue(type_system, Kind::ComptimeInt,
               type_system->UnwrapType(type_system->GetComptimeIntType())),
      m_words(value.isZero()
                  ? 0
                  : llvm::APInt::getNumWords(value.getSignificantBits())) {
  std::uninitialized_copy_n(value.extOrTrunc(GetWordsBitSize()).getRawData(),
                            m_words,
                            getTrailingObjects<llvm::APInt::WordType>());
}

ZigComptimeFloatType::ZigComptimeFloatType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::ComptimeFloatType, 0, llvm::Align()) {}

void ZigComptimeFloatType::PrintName(Stream &s) const { s << "comptime_float"; }

ZigComptimeFloat::ZigComptimeFloat(TypeSystemZig *type_system,
                                   const llvm::APFloat &value)
    : ZigValue(type_system, Kind::ComptimeFloat,
               type_system->UnwrapType(type_system->GetComptimeFloatType())) {
  std::uninitialized_copy_n(value.bitcastToAPInt().getRawData(),
                            std::size(m_words), m_words);
}

ZigUndefinedType::ZigUndefinedType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::UndefinedType, 0, llvm::Align()) {}

void ZigUndefinedType::PrintName(Stream &s) const { s << "@TypeOf(undefined)"; }

ZigNullType::ZigNullType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::NullType, 0, llvm::Align()) {}

void ZigNullType::PrintName(Stream &s) const { s << "@TypeOf(null)"; }

ZigAnyOpaqueType::ZigAnyOpaqueType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::AnyOpaqueType, 0, llvm::Align()) {}

void ZigAnyOpaqueType::PrintName(Stream &s) const { s << "anyopaque"; }

ZigEnumLiteralType::ZigEnumLiteralType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::EnumLiteralType, 0, llvm::Align()) {}

void ZigEnumLiteralType::PrintName(Stream &s) const {
  s << "@Type(.enum_literal)";
}

ZigEnumLiteral::ZigEnumLiteral(TypeSystemZig *type_system, ConstString value)
    : ZigValue(type_system, Kind::EnumLiteral,
               type_system->UnwrapType(type_system->GetEnumLiteralType())),
      m_value(value) {}

ZigAnyType::ZigAnyType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::AnyType, 0, llvm::Align()) {}

void ZigAnyType::PrintName(Stream &s) const { s << "anytype"; }

ZigBoolType::ZigBoolType(TypeSystemZig *type_system)
    : ZigType(type_system, Kind::BoolType, 1, llvm::Align()) {}

void ZigBoolType::PrintName(Stream &s) const { s << "bool"; }

ZigBool::ZigBool(TypeSystemZig *type_system, bool value)
    : ZigValue(type_system, value ? Kind::BoolTrue : Kind::BoolFalse,
               type_system->UnwrapType(type_system->GetBoolType())) {}

ZigIntType::ZigIntType(TypeSystemZig *type_system, ConstString name,
                       bool is_signed, uint16_t bit_size, uint16_t byte_size,
                       llvm::Align align)
    : ZigType(type_system, Kind::IntType, byte_size, align),
      m_is_signed(is_signed), m_bit_size(bit_size) {
  SetName(name);
}

void ZigIntType::PrintName(Stream &s) const {
  s.Format("{0}{1:d}", char(IsSigned() ? 'i' : 'u'), GetBitSize());
}

ZigInt::ZigInt(TypeSystemZig *type_system, ZigIntType *type,
               const llvm::APInt &value)
    : ZigValue(type_system, Kind::Int, type) {
  assert(type->GetBitSize() == value.getBitWidth() &&
         "type and value bit width mismatch");
  std::uninitialized_copy_n(value.getRawData(), value.getNumWords(),
                            getTrailingObjects<llvm::APInt::WordType>());
}

ZigFloatType::ZigFloatType(TypeSystemZig *type_system, ConstString name,
                           uint16_t bit_size, uint16_t byte_size,
                           llvm::Align align)
    : ZigType(type_system, Kind::FloatType, byte_size, align),
      m_bit_size(bit_size) {
  SetName(name);
}

void ZigFloatType::PrintName(Stream &s) const {
  s.Format("f{0:d}", GetBitSize());
}

ZigFloat::ZigFloat(TypeSystemZig *type_system, ZigFloatType *type,
                   const llvm::APFloat &value)
    : ZigValue(type_system, Kind::Float, type) {
  assert(&type->GetSemantics() == &value.getSemantics() &&
         "semantics mismatch");
  llvm::APInt bits = value.bitcastToAPInt();
  std::uninitialized_copy_n(bits.getRawData(), bits.getNumWords(),
                            getTrailingObjects<llvm::APInt::WordType>());
}

static uint64_t OptionalTypeByteSize(ZigType *child_type) {
  if (llvm::isa<ZigErrorSetType>(child_type))
    return child_type->GetByteSize();
  if (ZigPointerType *pointer_type = llvm::dyn_cast<ZigPointerType>(child_type))
    if (!pointer_type->IsAllowZero())
      return child_type->GetByteSize();
  return alignTo(child_type->GetByteSize() + 1, child_type->GetAlign());
}

ZigOptionalType::ZigOptionalType(TypeSystemZig *type_system,
                                 ZigType *child_type)
    : ZigType(type_system, Kind::OptionalType, OptionalTypeByteSize(child_type),
              child_type->GetAlign()),
      m_child_type(child_type) {}

void ZigOptionalType::PrintName(Stream &s) const {
  s << '?' << GetChildType()->GetQualifiedName();
}

ZigPointerType::ZigPointerType(TypeSystemZig *type_system, Size size,
                               ZigValue *sentinel, bool is_allowzero,
                               llvm::MaybeAlign pointer_align,
                               AddressSpace addrspace, bool is_const,
                               bool is_volatile, ZigType *child_type,
                               uint8_t byte_size, llvm::Align align)
    : ZigType(type_system, Kind::PointerType, byte_size, align),
      m_sentinel(sentinel), m_child_type(child_type), m_size(size),
      m_is_allowzero(size == Size::C || is_allowzero), m_is_const(is_const),
      m_is_volatile(is_volatile),
      m_align(pointer_align.value_or(child_type->GetAlign())),
      m_addrspace(addrspace) {
  assert((!sentinel || sentinel->GetType() == child_type) &&
         "incorrect sentinel type");
}

void ZigPointerType::PrintName(Stream &s) const {
  switch (GetSize()) {
  case ZigPointerType::Size::One:
    s << '*';
    break;
  case ZigPointerType::Size::Many:
    s << "[*";
    break;
  case ZigPointerType::Size::Slice:
    s << '[';
    break;
  case ZigPointerType::Size::C:
    s << "[*c";
    break;
  }
  if (ZigInt *sentinel_int = llvm::dyn_cast_if_present<ZigInt>(GetSentinel()))
    s.AsRawOstream() << ':' << sentinel_int->GetValue();
  if (GetSize() != ZigPointerType::Size::One)
    s << ']';
  if (GetSize() != ZigPointerType::Size::C && IsAllowZero())
    s << "allowzero ";
  if (llvm::Align pointer_align = GetPointerAlign();
      pointer_align != GetChildType()->GetAlign())
    s.Format("align({0:d}) ", pointer_align.value());
  switch (GetAddressSpace()) {
  case ZigPointerType::AddressSpace::Generic:
    break;
  case ZigPointerType::AddressSpace::Gs:
    s << "addrspace(.gs) ";
    break;
  case ZigPointerType::AddressSpace::Fs:
    s << "addrspace(.fs) ";
    break;
  case ZigPointerType::AddressSpace::Ss:
    s << "addrspace(.ss) ";
    break;
  case ZigPointerType::AddressSpace::Global:
    s << "addrspace(.global) ";
    break;
  case ZigPointerType::AddressSpace::Constant:
    s << "addrspace(.constant) ";
    break;
  case ZigPointerType::AddressSpace::Param:
    s << "addrspace(.param) ";
    break;
  case ZigPointerType::AddressSpace::Shared:
    s << "addrspace(.shared) ";
    break;
  case ZigPointerType::AddressSpace::Local:
    s << "addrspace(.local) ";
    break;
  case ZigPointerType::AddressSpace::Input:
    s << "addrspace(.input) ";
    break;
  case ZigPointerType::AddressSpace::Output:
    s << "addrspace(.output) ";
    break;
  case ZigPointerType::AddressSpace::Uniform:
    s << "addrspace(.uniform) ";
    break;
  case ZigPointerType::AddressSpace::Flash:
    s << "addrspace(.flash) ";
    break;
  case ZigPointerType::AddressSpace::Flash1:
    s << "addrspace(.flash1) ";
    break;
  case ZigPointerType::AddressSpace::Flash2:
    s << "addrspace(.flash2) ";
    break;
  case ZigPointerType::AddressSpace::Flash3:
    s << "addrspace(.flash3) ";
    break;
  case ZigPointerType::AddressSpace::Flash4:
    s << "addrspace(.flash4) ";
    break;
  case ZigPointerType::AddressSpace::Flash5:
    s << "addrspace(.flash5) ";
    break;
  }
  if (IsConst())
    s << "const ";
  if (IsVolatile())
    s << "volatile ";
  s << GetChildType()->GetQualifiedName();
}

ZigPointer::ZigPointer(TypeSystemZig *type_system, ZigPointerType *type,
                       ZigValue *pointee, uint64_t offset)
    : ZigPointer(type_system, Kind::Pointer, type, pointee, offset) {
  assert(type->GetSize() != ZigPointerType::Size::Slice &&
         "expected non-slice pointer type");
}

ZigPointer::ZigPointer(TypeSystemZig *type_system, Kind kind,
                       ZigPointerType *type, ZigValue *pointee, uint64_t offset)
    : ZigValue(type_system, kind, type), m_pointee(pointee), m_offset(offset) {
  assert(classof(this) && "invalid kind");
}

ZigSlice::ZigSlice(TypeSystemZig *type_system, ZigPointerType *type,
                   ZigValue *pointee, uint64_t offset, uint64_t len)
    : ZigPointer(type_system, Kind::Slice, type, pointee, offset), m_len(len) {
  assert(type->GetSize() == ZigPointerType::Size::Slice &&
         "expected slice pointer type");
}

ZigTagField::ZigTagField(ConstString name, ZigInt *value)
    : m_name(name), m_value(value) {}

ZigTagType::ZigTagType(TypeSystemZig *type_system, Kind kind, ConstString name,
                       ZigIntType *backing_type, uint32_t num_fields)
    : ZigType(type_system, kind, backing_type->GetByteSize(),
              backing_type->GetAlign()),
      m_backing_type(backing_type), m_num_fields(num_fields) {
  assert(classof(this) && "invalid kind");
  SetName(name);
}

ZigTag::ZigTag(TypeSystemZig *type_system, ZigTagType *type, ZigInt *value)
    : ZigValue(type_system, Kind::Tag, type), m_value(value) {
  assert(type->GetBackingType() == value->GetType() &&
         "backing type and value type mismatch");
}

template <typename DerivedType>
llvm::ArrayRef<ZigTagField> ZigTagTypeImpl<DerivedType>::GetFields() const {
  return llvm::ArrayRef<ZigTagField>(
      TrailingObjects::template getTrailingObjects<ZigTagField>(),
      GetNumFields());
}

template <typename DerivedType>
ZigTagTypeImpl<DerivedType>::ZigTagTypeImpl(TypeSystemZig *type_system,
                                            ConstString name,
                                            ZigIntType *backing_type,
                                            llvm::ArrayRef<ZigTagField> fields)
    : ZigTagType(type_system, DerivedType::kind, name, backing_type,
                 fields.size()) {
  std::uninitialized_copy(
      fields.begin(), fields.end(),
      TrailingObjects::template getTrailingObjects<ZigTagField>());
}

template class lldb_private::ZigTagTypeImpl<ZigErrorSetType>;
template class lldb_private::ZigTagTypeImpl<ZigGeneratedTagType>;
template class lldb_private::ZigTagTypeImpl<ZigEnumType>;

ZigNamespace::ZigNamespace(TypeSystemZig *type_system, ZigType *owner)
    : ZigNamespace(type_system, Kind::Namespace, owner) {}

ZigNamespace::ZigNamespace(TypeSystemZig *type_system, Kind kind,
                           ZigType *owner)
    : ZigScope(type_system, kind), m_owner(owner) {
  assert(classof(this) && "invalid kind");
}

ZigErrorSetType::ZigErrorSetType(TypeSystemZig *type_system, ConstString name,
                                 ZigIntType *backing_type,
                                 llvm::ArrayRef<ZigTagField> fields)
    : ZigTagTypeImpl(type_system, name, backing_type, fields),
      m_namespace(type_system, this) {}

ZigErrorUnionType::ZigErrorUnionType(TypeSystemZig *type_system,
                                     ZigErrorSetType *error_set,
                                     ZigType *payload)
    : ZigType(type_system, Kind::ErrorUnionType,
              alignTo(error_set->GetByteSize(), payload->GetAlign()) +
                  alignTo(payload->GetByteSize(), error_set->GetAlign()),
              std::max(error_set->GetAlign(), payload->GetAlign())),
      m_error_set(error_set), m_payload(payload) {}

void ZigErrorUnionType::PrintName(Stream &s) const {
  s << GetErrorSet()->GetQualifiedName() << '!'
    << GetPayload()->GetQualifiedName();
}

ZigSequenceType::ZigSequenceType(TypeSystemZig *type_system, Kind kind,
                                 uint64_t len, ZigType *child_type,
                                 bool has_sentinel)
    : ZigType(type_system, kind,
              child_type->GetByteSize() * (len + has_sentinel),
              child_type->GetAlign()),
      m_len(len), m_child_type(child_type) {
  assert(classof(this) && "invalid kind");
}

ZigArrayType::ZigArrayType(TypeSystemZig *type_system, uint64_t len,
                           ZigValue *sentinel, ZigType *child_type)
    : ZigSequenceType(type_system, Kind::ArrayType, len, child_type, sentinel),
      m_sentinel(sentinel) {
  assert((!sentinel || sentinel->GetType() == child_type) &&
         "incorrect sentinel type");
}

void ZigArrayType::PrintName(Stream &s) const {
  s.Format("[{0:d}", GetLength());
  if (ZigInt *sentinel_int = llvm::dyn_cast_if_present<ZigInt>(GetSentinel()))
    s.AsRawOstream() << ':' << sentinel_int->GetValue();
  s << ']' << GetChildType()->GetQualifiedName();
}

ZigVectorType::ZigVectorType(TypeSystemZig *type_system, uint32_t len,
                             ZigType *child_type)
    : ZigSequenceType(type_system, Kind::VectorType, len, child_type, false) {}

void ZigVectorType::PrintName(Stream &s) const {
  s.Format("@Vector({0:d}, {1})", GetLength(),
           GetChildType()->GetQualifiedName());
}

ZigFunctionType::ZigFunctionType(TypeSystemZig *type_system,
                                 llvm::ArrayRef<ZigType *> param_types,
                                 bool is_var_args, ZigType *ret_type)
    : ZigType(type_system, Kind::FunctionType, 0, llvm::Align()),
      m_is_var_args(is_var_args), m_num_params(param_types.size()),
      m_ret_type(ret_type) {
  std::uninitialized_copy(param_types.begin(), param_types.end(),
                          getTrailingObjects<ZigType *>());
}

void ZigFunctionType::PrintName(Stream &s) const {
  bool need_comma = false;
  s << "fn (";
  for (ZigType *param_type : GetParamTypes()) {
    s << (need_comma ? ", " : "") << param_type->GetQualifiedName();
    need_comma = true;
  }
  if (IsVarArgs())
    s << (need_comma ? ", " : "") << "...";
  s << ") " << GetReturnType()->GetQualifiedName();
}

ZigGeneratedTagType::ZigGeneratedTagType(TypeSystemZig *type_system,
                                         ConstString name,
                                         ZigIntType *backing_type,
                                         llvm::ArrayRef<ZigTagField> fields)
    : ZigTagTypeImpl(type_system, name, backing_type, fields),
      m_namespace(type_system, this) {}

ZigContainer::ZigContainer(TypeSystemZig *type_system, ZigType *owner)
    : ZigNamespace(type_system, Kind::Container, owner), m_parent(nullptr) {}

void ZigContainer::UpdateParent() const {
  assert(!m_parent && "parent already updated");
  GetTypeSystem()->TypeUpdateParent(GetOwner()->AsOpaqueType());
}

ZigEnumType::ZigEnumType(TypeSystemZig *type_system, ConstString name,
                         ZigIntType *backing_type,
                         llvm::ArrayRef<ZigTagField> fields)
    : ZigTagTypeImpl(type_system, name, backing_type, fields),
      m_container(type_system, this) {}

ZigTupleType::ZigTupleType(TypeSystemZig *type_system,
                           llvm::ArrayRef<ZigTupleField> fields,
                           uint64_t byte_size, llvm::Align align)
    : ZigType(type_system, Kind::TupleType, byte_size, align),
      m_num_fields(fields.size()) {
  std::uninitialized_copy(fields.begin(), fields.end(),
                          getTrailingObjects<ZigTupleField>());
}

void ZigTupleType::PrintName(Stream &s) const {
  TypeSystemZig *type_system = GetTypeSystem();
  bool need_comma = false;
  s << "struct {";
  for (const ZigTupleField &field : GetFields()) {
    if (need_comma)
      s << ',';
    s << ' ';
    if (field.IsComptime())
      s << "comptime ";
    s << field.GetType()->GetQualifiedName();
    if (ZigValue *field_comptime_value = field.GetComptimeValue()) {
      s << " = ";
      type_system->DumpValue(field_comptime_value, s);
    }
    need_comma = true;
  }
  if (need_comma)
    s << ' ';
  s << '}';
}

ZigRecordType::ZigRecordType(TypeSystemZig *type_system, Kind kind,
                             ConstString name, uint64_t byte_size,
                             llvm::Align align)
    : ZigType(type_system, kind, byte_size, align),
      m_container(type_system, this) {
  assert(classof(this) && "invalid kind");
  SetName(name);
}

void ZigRecordType::UpdateFields() const {
  assert(!m_fields.data() && "fields already updated");
  GetTypeSystem()->GetCompleteType(
      const_cast<ZigRecordType *>(this)->AsOpaqueType());
}

ZigStructType::ZigStructType(TypeSystemZig *type_system, ConstString name,
                             uint64_t byte_size, llvm::Align align)
    : ZigStructType(type_system, Kind::StructType, name, byte_size, align) {}

ZigStructType::ZigStructType(TypeSystemZig *type_system, Kind kind,
                             ConstString name, uint64_t byte_size,
                             llvm::Align align)
    : ZigRecordType(type_system, kind, name, byte_size, align) {
  assert(classof(this) && "invalid kind");
}

ZigPackedStructType::ZigPackedStructType(TypeSystemZig *type_system,
                                         ConstString name,
                                         ZigIntType *backing_type)
    : ZigStructType(type_system, Kind::PackedStructType, name,
                    backing_type->GetByteSize(), backing_type->GetAlign()),
      m_backing_type(backing_type) {}

ZigUnionType::ZigUnionType(TypeSystemZig *type_system, ConstString name,
                           uint64_t byte_size, llvm::Align align)
    : ZigUnionType(type_system, Kind::UnionType, name, byte_size, align) {}

ZigUnionType::ZigUnionType(TypeSystemZig *type_system, Kind kind,
                           ConstString name, uint64_t byte_size,
                           llvm::Align align)
    : ZigRecordType(type_system, kind, name, byte_size, align) {
  assert(classof(this) && "invalid kind");
}

ZigTaggedUnionType::ZigTaggedUnionType(TypeSystemZig *type_system,
                                       ConstString name, ZigTagType *tag_type,
                                       uint32_t tag_byte_offset,
                                       uint64_t byte_size, llvm::Align align)
    : ZigUnionType(type_system, Kind::TaggedUnionType, name, byte_size, align),
      m_tag_type(tag_type), m_tag_byte_offset(tag_byte_offset) {}

ZigPackedUnionType::ZigPackedUnionType(TypeSystemZig *type_system,
                                       ConstString name,
                                       ZigIntType *backing_type)
    : ZigUnionType(type_system, Kind::PackedUnionType, name,
                   backing_type->GetByteSize(), backing_type->GetAlign()),
      m_backing_type(backing_type) {}
