//===-- ZigValue.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_ZIGVALUE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_ZIGVALUE_H

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TrailingObjects.h"

#include <cstdint>

namespace lldb_private {

class TypeSystemZig;
class ZigDeclaration;
class ZigFunctionType;
class ZigNamespace;
class ZigType;
class ZigTypeType;

class ZigScope {
public:
  enum class Kind : uint8_t {
    Module,
    Namespace,
    Container,
    Function,
    InlinedBlock,
    Block,

    FirstNamespace = Namespace,
    LastNamespace = Container,
  };

  virtual ~ZigScope() = default;

  void *AsOpaqueDeclContext() { return this; }
  Kind GetKind() const { return m_kind; }
  virtual ZigScope *GetParent() const = 0;
  virtual TypeSystemZig *GetTypeSystem() const = 0;
  virtual ConstString GetName() const = 0;
  ConstString GetQualifiedName() const {
    if (m_qualified_name.IsNull())
      UpdateQualifiedName();
    return m_qualified_name;
  }
  ZigDeclaration *GetFirstDecl() const { return m_first_decl; }

protected:
  ZigScope(TypeSystemZig *type_system, Kind kind);

private:
  friend ZigDeclaration;

  void UpdateQualifiedName() const;

  const Kind m_kind;
  mutable ConstString m_qualified_name;
  ZigDeclaration *m_first_decl;
  ZigDeclaration *m_last_decl;
};

class ZigModule final : public ZigScope {
public:
  static bool classof(const ZigScope *scope) {
    return scope->GetKind() == Kind::Module;
  }

  ZigModule(TypeSystemZig *type_system, ConstString name);

  ZigScope *GetParent() const final { return nullptr; }
  TypeSystemZig *GetTypeSystem() const final { return m_type_system; }
  ConstString GetName() const final { return m_name; }

private:
  TypeSystemZig *m_type_system;
  ConstString m_name;
};

class ZigValue {
public:
  enum class Kind : uint8_t {
    Constant,
    Alias,
    Variable,
    Function,
    Data,
    OnlyPossibleValue,
    ComptimeInt,
    ComptimeFloat,
    EnumLiteral,
    BoolFalse,
    BoolTrue,
    Int,
    Float,
    Pointer,
    Slice,
    Tag,
    TypeType,
    VoidType,
    NoReturnType,
    ComptimeIntType,
    ComptimeFloatType,
    UndefinedType,
    NullType,
    AnyOpaqueType,
    EnumLiteralType,
    AnyType,
    BoolType,
    IntType,
    FloatType,
    OptionalType,
    PointerType,
    ErrorUnionType,
    ArrayType,
    VectorType,
    FunctionType,
    ErrorSetType,
    GeneratedTagType,
    EnumType,
    TupleType,
    StructType,
    PackedStructType,
    UnionType,
    TaggedUnionType,
    PackedUnionType,

    FirstConstant = Constant,
    LastConstant = Alias,

    FirstDeclaration = Constant,
    LastDeclaration = Function,

    FirstBool = BoolFalse,
    LastBool = BoolTrue,

    FirstPointer = Pointer,
    LastPointer = Slice,

    FirstType = TypeType,
    LastType = PackedUnionType,

    FirstSequenceType = ArrayType,
    LastSequenceType = VectorType,

    FirstTagType = ErrorSetType,
    LastTagType = EnumType,

    FirstRecordType = StructType,
    LastRecordType = PackedUnionType,

    FirstStructType = StructType,
    LastStructType = PackedStructType,

    FirstUnionType = UnionType,
    LastUnionType = PackedUnionType,
  };

  class Marker {
  public:
    ~Marker() { m_value.m_mark = m_was_marked; }

    operator bool() const { return !m_was_marked; }

  private:
    friend ZigValue;

    explicit Marker(const ZigValue &value)
        : m_value(value), m_was_marked(value.m_mark) {
      value.m_mark = true;
    }

    Marker(const Marker &) = delete;
    Marker &operator=(const Marker &) = delete;

    const ZigValue &m_value;
    bool m_was_marked;
  };

  virtual ~ZigValue() = default;

  lldb::opaque_compiler_type_t AsOpaqueType() { return this; }
  Kind GetKind() const { return m_kind; }
  Marker Mark() const { return Marker(*this); }
  ZigType *GetType() const { return m_type; }
  TypeSystemZig *GetTypeSystem() const;
  virtual ZigNamespace *GetNamespace() { return nullptr; }
  const ZigNamespace *GetNamespace() const {
    return const_cast<ZigValue *>(this)->GetNamespace();
  }

protected:
  ZigValue(TypeSystemZig *type_system, Kind kind, ZigType *type);

private:
  friend Marker;

  const Kind m_kind;
  mutable bool m_mark;
  ZigType *m_type;
};

class ZigDeclaration : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstDeclaration &&
           value->GetKind() <= Kind::LastDeclaration;
  }

  ZigScope *GetParent() const { return m_parent; }
  ZigDeclaration *GetSibling() const { return m_sibling; }
  ConstString GetName() const { return m_name; }

protected:
  ZigDeclaration(TypeSystemZig *type_system, Kind kind, ZigScope *parent,
                 ConstString name, ZigType *type);

private:
  ZigScope *m_parent;
  ZigDeclaration *m_sibling;
  ConstString m_name;
};

class ZigConstant : public ZigDeclaration {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstConstant &&
           value->GetKind() <= Kind::LastConstant;
  }

  ZigConstant(TypeSystemZig *type_system, ZigScope *parent, ConstString name,
              ZigValue *value);

  ZigValue *GetValue() const { return m_value; }

protected:
  ZigConstant(TypeSystemZig *type_system, Kind kind, ZigScope *parent,
              ConstString name, ZigValue *value);

private:
  ZigValue *m_value;
};

class ZigAlias final : public ZigConstant {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Alias;
  }

  ZigAlias(TypeSystemZig *type_system, ZigScope *parent, ConstString name,
           ZigType *value);

  ZigType *GetValue() const {
    return llvm::cast<ZigType>(ZigConstant::GetValue());
  }
};

class ZigVariable final : public ZigDeclaration {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Variable;
  }

  ZigVariable(TypeSystemZig *type_system, ZigScope *parent, ConstString name,
              ZigType *type);
};

class ZigFunction final : public ZigScope, public ZigDeclaration {
public:
  static bool classof(const ZigScope *scope) {
    return scope->GetKind() == ZigScope::Kind::Function;
  }
  static bool classof(const ZigValue *value) {
    return value->GetKind() == ZigValue::Kind::Function;
  }

  ZigFunction(TypeSystemZig *type_system, ZigScope *parent, ConstString name,
              ZigFunctionType *type);

  TypeSystemZig *GetTypeSystem() const final {
    return ZigDeclaration::GetTypeSystem();
  }
  ZigFunctionType *GetType() const {
    return llvm::cast<ZigFunctionType>(ZigValue::GetType());
  }
  ZigScope *GetParent() const final { return ZigDeclaration::GetParent(); }
  ConstString GetName() const final { return ZigDeclaration::GetName(); }
};

class ZigBlock final : public ZigScope {
public:
  static bool classof(const ZigScope *scope) {
    return scope->GetKind() == ZigScope::Kind::Block;
  }

  ZigBlock(TypeSystemZig *type_system, ZigScope *parent);

  TypeSystemZig *GetTypeSystem() const final {
    return GetParent()->GetTypeSystem();
  }
  ZigScope *GetParent() const final { return m_parent; }
  ConstString GetName() const final { llvm_unreachable("blocks are unnamed"); }

private:
  ZigScope *m_parent;
};

class ZigType : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstType &&
           value->GetKind() <= Kind::LastType;
  }

  TypeSystemZig *GetTypeSystem() const;
  ZigTypeType *GetType() const {
    return llvm::cast<ZigTypeType>(ZigValue::GetType());
  }
  ConstString GetName() const {
    if (m_name.IsNull())
      UpdateName();
    return m_name;
  }
  ConstString GetQualifiedName() const;
  uint64_t GetByteSize() const { return m_byte_size; }
  virtual uint64_t GetBitSize() const { return m_byte_size * 8; }
  llvm::Align GetAlign() const { return m_align; }
  virtual bool HasNoPossibleValues() const = 0;
  virtual bool HasOnePossibleValue() const = 0;
  virtual bool HasComptimeState() const = 0;

protected:
  ZigType(TypeSystemZig *type_system, Kind kind, ZigTypeType *type,
          uint64_t byte_size, llvm::Align align);
  ZigType(TypeSystemZig *type_system, Kind kind, uint64_t byte_size,
          llvm::Align align);

  void SetName(ConstString name) { m_name = name; }
  virtual void PrintName(Stream &s) const = 0;

private:
  void UpdateName() const;

  mutable ConstString m_name;
  uint64_t m_byte_size;
  llvm::Align m_align;
};

class ZigData final : public ZigValue,
                      private llvm::TrailingObjects<ZigData, uint8_t> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Data;
  }

  ZigData(TypeSystemZig *type_system, ZigType *type,
          llvm::ArrayRef<uint8_t> data);

  llvm::ArrayRef<uint8_t> GetData() const {
    return llvm::ArrayRef(getTrailingObjects<uint8_t>(),
                          GetType()->GetByteSize());
  }

private:
  friend TrailingObjects;
  friend TypeSystemZig;
};

class ZigOnlyPossibleValue final : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::OnlyPossibleValue;
  }

  ZigOnlyPossibleValue(TypeSystemZig *type_system, ZigType *type);
};

class ZigTypeType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::TypeType;
  }

  ZigTypeType(TypeSystemZig *m_type_system);

  TypeSystemZig *GetTypeSystem() const { return m_type_system; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;

private:
  TypeSystemZig *m_type_system;
};

class ZigVoidType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::VoidType;
  }

  ZigVoidType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return true; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigNoReturnType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::NoReturnType;
  }

  ZigNoReturnType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return true; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigComptimeIntType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ComptimeIntType;
  }

  ZigComptimeIntType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigComptimeInt final
    : public ZigValue,
      private llvm::TrailingObjects<ZigComptimeInt, llvm::APInt::WordType> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ComptimeInt;
  }

  ZigComptimeInt(TypeSystemZig *type_system, const llvm::APSInt &value);

  ZigComptimeIntType *GetType() const {
    return llvm::cast<ZigComptimeIntType>(ZigValue::GetType());
  }
  llvm::ArrayRef<llvm::APInt::WordType> GetWords() const {
    return llvm::ArrayRef(getTrailingObjects<llvm::APInt::WordType>(), m_words);
  }
  uint64_t GetWordsBitSize() const {
    return llvm::APInt::APINT_BITS_PER_WORD * m_words;
  }

  llvm::APSInt GetValue() const {
    return m_words == 0
               ? llvm::APSInt(0, false)
               : llvm::APSInt(llvm::APInt(GetWordsBitSize(), GetWords()),
                              false);
  }

private:
  friend TrailingObjects;
  friend TypeSystemZig;

  uint32_t m_words;
};

class ZigComptimeFloatType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ComptimeFloatType;
  }

  ZigComptimeFloatType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigComptimeFloat final : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ComptimeFloat;
  }

  ZigComptimeFloat(TypeSystemZig *type_system, const llvm::APFloat &value);

  ZigComptimeFloatType *GetType() const {
    return llvm::cast<ZigComptimeFloatType>(ZigValue::GetType());
  }
  llvm::ArrayRef<llvm::APInt::WordType> GetWords() const { return m_words; }
  llvm::APFloat GetValue() const {
    return llvm::APFloat(llvm::APFloat::IEEEquad(),
                         llvm::APInt(128, GetWords()));
  }

private:
  llvm::APInt::WordType m_words[llvm::APInt::getNumWords(128)];
};

class ZigUndefinedType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::UndefinedType;
  }

  ZigUndefinedType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return true; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigNullType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::NullType;
  }

  ZigNullType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return true; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigAnyOpaqueType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::AnyOpaqueType;
  }

  ZigAnyOpaqueType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigEnumLiteralType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::EnumLiteralType;
  }

  ZigEnumLiteralType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigEnumLiteral final : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::EnumLiteral;
  }

  ZigEnumLiteral(TypeSystemZig *type_system, ConstString value);

  ZigEnumLiteralType *GetType() const {
    return llvm::cast<ZigEnumLiteralType>(ZigValue::GetType());
  }
  ConstString GetValue() const { return m_value; }

private:
  ConstString m_value;
};

class ZigAnyType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::AnyType;
  }

  ZigAnyType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigBoolType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::BoolType;
  }

  uint64_t GetBitSize() const final { return 1; }

  ZigBoolType(TypeSystemZig *type_system);

  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigBool final : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstBool &&
           value->GetKind() <= Kind::LastBool;
  }

  ZigBool(TypeSystemZig *type_system, bool value);

  ZigBoolType *GetType() const {
    return llvm::cast<ZigBoolType>(ZigValue::GetType());
  }
  bool GetValue() const {
    switch (GetKind()) {
    default:
      llvm_unreachable("invalid kind");
    case Kind::BoolFalse:
      return false;
    case Kind::BoolTrue:
      return true;
    }
  }
};

class ZigIntType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::IntType;
  }

  ZigIntType(TypeSystemZig *type_system, ConstString name, bool is_signed,
             uint16_t bit_size, uint16_t byte_size, llvm::Align align);

  bool IsSigned() const { return m_is_signed; }
  uint64_t GetBitSize() const final { return m_bit_size; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return false; }

  bool Fits(const llvm::APSInt &value) {
    if (value.isZero())
      return true;
    if (uint64_t bit_size = GetBitSize())
      return IsSigned() ? value.isSignedIntN(bit_size)
                        : value.isNonNegative() && value.isIntN(bit_size);
    return false;
  }

protected:
  void PrintName(Stream &s) const final;

private:
  bool m_is_signed;
  uint16_t m_bit_size;
};

class ZigInt final
    : public ZigValue,
      private llvm::TrailingObjects<ZigInt, llvm::APInt::WordType> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Int;
  }

  ZigInt(TypeSystemZig *type_system, ZigIntType *type,
         const llvm::APInt &value);

  ZigIntType *GetType() const {
    return llvm::cast<ZigIntType>(ZigValue::GetType());
  }
  llvm::ArrayRef<llvm::APInt::WordType> GetWords() const {
    return llvm::ArrayRef(getTrailingObjects<llvm::APInt::WordType>(),
                          llvm::APInt::getNumWords(GetType()->GetBitSize()));
  }
  llvm::APSInt GetValue() const {
    ZigIntType *type = GetType();
    uint16_t bit_size = type->GetBitSize();
    bool is_unsigned = !type->IsSigned();
    return bit_size == 0
               ? llvm::APSInt(0, is_unsigned)
               : llvm::APSInt(llvm::APInt(bit_size, GetWords()), is_unsigned);
  }

private:
  friend TrailingObjects;
  friend TypeSystemZig;
};

class ZigFloatType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::FloatType;
  }

  ZigFloatType(TypeSystemZig *type_system, ConstString name, uint16_t bit_size,
               uint16_t byte_size, llvm::Align align);

  static const llvm::fltSemantics &GetSemantics(uint64_t bit_size) {
    switch (bit_size) {
    default:
      return llvm::APFloat::Bogus();
    case 16:
      return llvm::APFloat::IEEEhalf();
    case 32:
      return llvm::APFloat::IEEEsingle();
    case 64:
      return llvm::APFloat::IEEEdouble();
    case 80:
      return llvm::APFloat::x87DoubleExtended();
    case 128:
      return llvm::APFloat::IEEEquad();
    }
  }
  const llvm::fltSemantics &GetSemantics() const {
    return GetSemantics(GetBitSize());
  }
  uint64_t GetBitSize() const final { return m_bit_size; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return false; }

protected:
  void PrintName(Stream &s) const final;

private:
  uint16_t m_bit_size;
};

class ZigFloat final
    : public ZigValue,
      private llvm::TrailingObjects<ZigFloat, llvm::APInt::WordType> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Float;
  }

  ZigFloat(TypeSystemZig *type_system, ZigFloatType *type,
           const llvm::APFloat &value);

  ZigFloatType *GetType() const {
    return llvm::cast<ZigFloatType>(ZigValue::GetType());
  }
  llvm::ArrayRef<llvm::APInt::WordType> GetWords() const {
    return llvm::ArrayRef(getTrailingObjects<llvm::APInt::WordType>(),
                          llvm::APInt::getNumWords(GetType()->GetBitSize()));
  }
  llvm::APFloat GetValue() const {
    ZigFloatType *type = GetType();
    return llvm::APFloat(type->GetSemantics(),
                         llvm::APInt(type->GetBitSize(), GetWords()));
  }

private:
  friend TrailingObjects;
  friend TypeSystemZig;
};

class ZigOptionalType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::OptionalType;
  }

  ZigOptionalType(TypeSystemZig *type_system, ZigType *child_type);

  ZigType *GetChildType() const { return m_child_type; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final {
    return GetChildType()->HasNoPossibleValues();
  }
  bool HasComptimeState() const final {
    return Mark() && GetChildType()->HasComptimeState();
  }

protected:
  void PrintName(Stream &s) const final;

private:
  ZigType *m_child_type;
};

class ZigPointerType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::PointerType;
  }

  enum class Size : uint8_t { One, Many, Slice, C };
  enum class AddressSpace : uint8_t {
    Generic,
    Gs,
    Fs,
    Ss,
    Global,
    Constant,
    Param,
    Shared,
    Local,
    Input,
    Output,
    Uniform,
    Flash,
    Flash1,
    Flash2,
    Flash3,
    Flash4,
    Flash5,
  };

  ZigPointerType(TypeSystemZig *type_system, Size size, ZigValue *sentinel,
                 bool is_allowzero, llvm::MaybeAlign pointer_align,
                 AddressSpace addrspace, bool is_const, bool is_volatile,
                 ZigType *child_type, uint8_t byte_size, llvm::Align align);

  Size GetSize() const { return m_size; }
  ZigValue *GetSentinel() const { return m_sentinel; }
  bool IsAllowZero() const { return m_is_allowzero; }
  llvm::Align GetPointerAlign() const { return m_align; }
  AddressSpace GetAddressSpace() const { return m_addrspace; }
  bool IsConst() const { return m_is_const; }
  bool IsVolatile() const { return m_is_volatile; }
  ZigType *GetChildType() const { return m_child_type; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final {
    return !llvm::isa<ZigAnyOpaqueType>(GetChildType()) &&
           !llvm::isa<ZigFunctionType>(GetChildType()) && Mark() &&
           GetChildType()->HasComptimeState();
  }

protected:
  void PrintName(Stream &s) const final;

private:
  ZigValue *m_sentinel;
  ZigType *m_child_type;
  Size m_size : 2;
  bool m_is_allowzero : 1;
  bool m_is_const : 1;
  bool m_is_volatile : 1;
  llvm::Align m_align;
  AddressSpace m_addrspace;
};

class ZigPointer : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstPointer &&
           value->GetKind() <= Kind::LastPointer;
  }

  ZigPointer(TypeSystemZig *type_system, ZigPointerType *type,
             ZigValue *pointee, uint64_t offset);

  ZigPointerType *GetType() const {
    return llvm::cast<ZigPointerType>(ZigValue::GetType());
  }
  ZigValue *GetPointee() const { return m_pointee; }
  uint64_t GetOffset() const { return m_offset; }

protected:
  ZigPointer(TypeSystemZig *type_system, Kind kind, ZigPointerType *type,
             ZigValue *pointee, uint64_t offset);

private:
  ZigValue *m_pointee;
  uint64_t m_offset;
};

class ZigSlice final : public ZigPointer {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Slice;
  }

  ZigSlice(TypeSystemZig *type_system, ZigPointerType *type, ZigValue *pointee,
           uint64_t offset, uint64_t len);

  uint64_t GetLength() const { return m_len; }

private:
  uint64_t m_len;
};

class ZigTagField {
public:
  ZigTagField(ConstString name, ZigInt *value);

  ConstString GetName() const { return m_name; }
  ZigInt *GetValue() const { return m_value; }

private:
  ConstString m_name;
  ZigInt *m_value;
};

class ZigTagType : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstTagType &&
           value->GetKind() <= Kind::LastTagType;
  }

  ZigIntType *GetBackingType() const { return m_backing_type; }
  uint64_t GetBitSize() const final { return GetBackingType()->GetBitSize(); }
  uint32_t GetNumFields() const { return m_num_fields; }
  virtual llvm::ArrayRef<ZigTagField> GetFields() const = 0;
  bool HasNoPossibleValues() const final { return GetNumFields() == 0; }
  bool HasOnePossibleValue() const final { return GetNumFields() == 1; }
  bool HasComptimeState() const final {
    return GetBackingType()->HasComptimeState();
  }

protected:
  ZigTagType(TypeSystemZig *type_system, Kind kind, ConstString name,
             ZigIntType *backing_type, uint32_t num_fields);

  void PrintName(Stream &s) const final { llvm_unreachable("missing name"); }

private:
  ZigIntType *m_backing_type;
  uint32_t m_num_fields;
};

class ZigTag final : public ZigValue {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::Tag;
  }

  ZigTag(TypeSystemZig *type_system, ZigTagType *type, ZigInt *value);

  ZigTagType *GetType() const {
    return llvm::cast<ZigTagType>(ZigValue::GetType());
  }
  ZigInt *GetValue() const { return m_value; }

private:
  ZigInt *m_value;
};

template <typename DerivedType>
class ZigTagTypeImpl
    : public ZigTagType,
      protected llvm::TrailingObjects<DerivedType, ZigTagField> {
  using TrailingObjects = llvm::TrailingObjects<DerivedType, ZigTagField>;

public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == DerivedType::kind;
  }

  llvm::ArrayRef<ZigTagField> GetFields() const final;

protected:
  ZigTagTypeImpl(TypeSystemZig *type_system, ConstString name,
                 ZigIntType *backing_type, llvm::ArrayRef<ZigTagField> fields);

private:
  friend TrailingObjects;
  friend TypeSystemZig;
};

class ZigNamespace : public ZigScope {
public:
  static bool classof(const ZigScope *scope) {
    return scope->GetKind() >= Kind::FirstNamespace &&
           scope->GetKind() <= Kind::LastNamespace;
  }

  ZigNamespace(TypeSystemZig *type_system, ZigType *owner);

  ZigScope *GetParent() const override { return nullptr; }
  ZigType *GetOwner() const { return m_owner; }
  TypeSystemZig *GetTypeSystem() const final {
    return GetOwner()->GetTypeSystem();
  }
  ConstString GetName() const final { return GetOwner()->GetName(); }

protected:
  ZigNamespace(TypeSystemZig *type_system, Kind kind, ZigType *owner);

private:
  ZigType *m_owner;
};

class ZigErrorSetType final : public ZigTagTypeImpl<ZigErrorSetType> {
public:
  static constexpr Kind kind = Kind::ErrorSetType;

  ZigErrorSetType(TypeSystemZig *type_system, ConstString name,
                  ZigIntType *backing_type, llvm::ArrayRef<ZigTagField> fields);

  ZigNamespace *GetNamespace() final { return &m_namespace; }

private:
  ZigNamespace m_namespace;
};

class ZigErrorUnionType final : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ErrorUnionType;
  }

  ZigErrorUnionType(TypeSystemZig *type_system, ZigErrorSetType *error_set,
                    ZigType *payload);

  ZigErrorSetType *GetErrorSet() const { return m_error_set; }
  ZigType *GetPayload() const { return m_payload; }
  bool HasNoPossibleValues() const final {
    return GetErrorSet()->HasNoPossibleValues() &&
           GetPayload()->HasNoPossibleValues();
  }
  bool HasOnePossibleValue() const final {
    return (GetErrorSet()->HasOnePossibleValue() &&
            GetPayload()->HasNoPossibleValues()) ||
           (GetErrorSet()->HasNoPossibleValues() &&
            GetPayload()->HasOnePossibleValue());
  }
  bool HasComptimeState() const final {
    return Mark() && (GetErrorSet()->HasComptimeState() ||
                      GetPayload()->HasComptimeState());
  }

protected:
  void PrintName(Stream &s) const final;

private:
  ZigErrorSetType *m_error_set;
  ZigType *m_payload;
};

class ZigSequenceType : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstSequenceType &&
           value->GetKind() <= Kind::LastSequenceType;
  }

  ZigSequenceType(TypeSystemZig *type_system, Kind kind, uint64_t len,
                  ZigType *child_type, bool has_sentinel);

  uint64_t GetLength() const { return m_len; }
  ZigType *GetChildType() const { return m_child_type; }
  virtual ZigValue *GetSentinel() const = 0;
  bool HasNoPossibleValues() const final {
    return GetLength() && GetChildType()->HasNoPossibleValues();
  }
  bool HasOnePossibleValue() const final {
    return !GetLength() || GetChildType()->HasOnePossibleValue();
  }
  bool HasComptimeState() const final {
    return GetLength() && Mark() && GetChildType()->HasComptimeState();
  }

private:
  uint64_t m_len;
  ZigType *m_child_type;
};

class ZigArrayType final : public ZigSequenceType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::ArrayType;
  }
  ZigArrayType(TypeSystemZig *type_system, uint64_t len, ZigValue *sentinel,
               ZigType *child_type);

  ZigValue *GetSentinel() const override { return m_sentinel; }

protected:
  void PrintName(Stream &s) const final;

private:
  ZigValue *m_sentinel;
};

class ZigVectorType final : public ZigSequenceType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::VectorType;
  }

  ZigVectorType(TypeSystemZig *type_system, uint32_t len, ZigType *child_type);

  ZigValue *GetSentinel() const override { return nullptr; }

protected:
  void PrintName(Stream &s) const final;
};

class ZigFunctionType final
    : public ZigType,
      private llvm::TrailingObjects<ZigFunctionType, ZigType *> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::FunctionType;
  }

  ZigFunctionType(TypeSystemZig *type_system,
                  llvm::ArrayRef<ZigType *> param_types, bool is_var_args,
                  ZigType *ret_type);

  llvm::ArrayRef<ZigType *> GetParamTypes() const {
    return llvm::ArrayRef<ZigType *>(getTrailingObjects<ZigType *>(),
                                     m_num_params);
  }
  bool IsVarArgs() const { return m_is_var_args; }
  ZigType *GetReturnType() const { return m_ret_type; }
  bool HasNoPossibleValues() const final { return false; }
  bool HasOnePossibleValue() const final { return false; }
  bool HasComptimeState() const final { return true; }

protected:
  void PrintName(Stream &s) const final;

private:
  friend TrailingObjects;
  friend TypeSystemZig;

  bool m_is_var_args : 1;
  uint32_t m_num_params : 31;
  ZigType *m_ret_type;
};

class ZigGeneratedTagType final : public ZigTagTypeImpl<ZigGeneratedTagType> {
public:
  static constexpr Kind kind = Kind::GeneratedTagType;

  ZigGeneratedTagType(TypeSystemZig *type_system, ConstString name,
                      ZigIntType *backing_type,
                      llvm::ArrayRef<ZigTagField> fields);

  ZigNamespace *GetNamespace() final { return &m_namespace; }

private:
  ZigNamespace m_namespace;
};

class ZigContainer final : public ZigNamespace {
public:
  static bool classof(const ZigScope *scope) {
    return scope->GetKind() == Kind::Container;
  }

  ZigContainer(TypeSystemZig *type_system, ZigType *owner);

  ZigScope *GetParent() const final {
    if (!m_parent)
      UpdateParent();
    return m_parent;
  }

private:
  friend TypeSystemZig;

  void UpdateParent() const;

  ZigScope *m_parent;
};

class ZigEnumType final : public ZigTagTypeImpl<ZigEnumType> {
public:
  static constexpr Kind kind = Kind::EnumType;
  using ZigTagTypeImpl::ZigTagTypeImpl;

  ZigEnumType(TypeSystemZig *type_system, ConstString name,
              ZigIntType *backing_type, llvm::ArrayRef<ZigTagField> fields);

  ZigNamespace *GetNamespace() final { return &m_container; }

private:
  ZigContainer m_container;
};

class ZigTupleField {
public:
  ZigTupleField(ZigType *type, ZigValue *comptime_value)
      : m_type(type), m_comptime_value(comptime_value) {}

  bool IsComptime() const { return m_comptime_value; }
  ZigType *GetType() const { return m_type; }
  ZigValue *GetComptimeValue() const { return m_comptime_value; }

  bool operator==(const ZigTupleField &that) const {
    return m_type == that.m_type && m_comptime_value == that.m_comptime_value;
  }
  bool operator!=(const ZigTupleField &that) const { return !(*this == that); }
  friend llvm::hash_code hash_value(const ZigTupleField &field) {
    return llvm::hash_combine(field.m_type, field.m_comptime_value);
  }

private:
  ZigType *m_type;
  ZigValue *m_comptime_value;
};

class ZigTupleType final
    : public ZigType,
      private llvm::TrailingObjects<ZigTupleType, ZigTupleField> {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::TupleType;
  }

  ZigTupleType(TypeSystemZig *type_system, llvm::ArrayRef<ZigTupleField> fields,
               uint64_t byte_size, llvm::Align align);

  llvm::ArrayRef<ZigTupleField> GetFields() const {
    return llvm::ArrayRef<ZigTupleField>(getTrailingObjects<ZigTupleField>(),
                                         m_num_fields);
  }
  bool HasNoPossibleValues() const final {
    for (const ZigTupleField &field : GetFields())
      if (field.GetType()->HasNoPossibleValues())
        return true;
    return false;
  }
  bool HasOnePossibleValue() const final {
    for (const ZigTupleField &field : GetFields())
      if (!field.IsComptime() && !field.GetType()->HasOnePossibleValue())
        return false;
    return true;
  }
  bool HasComptimeState() const final {
    if (Marker marker = Mark())
      for (const ZigTupleField &field : GetFields())
        if (field.IsComptime() || field.GetType()->HasComptimeState())
          return true;
    return false;
  }

protected:
  void PrintName(Stream &s) const final;

private:
  friend TrailingObjects;
  friend TypeSystemZig;

  uint32_t m_num_fields;
};

class ZigRecordField {
public:
  ZigRecordField(bool is_comptime, ConstString name, ZigType *type,
                 llvm::MaybeAlign align, ZigValue *default_value,
                 uint64_t bit_offset)
      : m_name(name), m_type(type), m_default_value(default_value),
        m_byte_offset(bit_offset >> 3), m_bit_offset(bit_offset),
        m_is_comptime(is_comptime), m_align(align) {}

  bool IsComptime() const { return m_is_comptime; }
  ConstString GetName() const { return m_name; }
  ZigType *GetType() const { return m_type; }
  llvm::Align GetAlign() const {
    return m_align.value_or(GetType()->GetAlign());
  }
  ZigValue *GetDefaultValue() const { return m_default_value; }
  uint64_t GetBitOffset() const {
    return uint64_t(m_byte_offset) * 8 + m_bit_offset;
  }

private:
  ConstString m_name;
  ZigType *m_type;
  ZigValue *m_default_value;
  uint32_t m_byte_offset;
  uint8_t m_bit_offset : 3;
  bool m_is_comptime : 1;
  llvm::MaybeAlign m_align;
};

class ZigRecordType : public ZigType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstRecordType &&
           value->GetKind() <= Kind::LastRecordType;
  }

  ZigNamespace *GetNamespace() final { return &m_container; }
  llvm::ArrayRef<ZigRecordField> GetFields() const {
    if (!m_fields.data())
      UpdateFields();
    return m_fields;
  }
  bool HasNoPossibleValues() const final {
    for (const ZigRecordField &field : GetFields())
      if (field.GetType()->HasNoPossibleValues())
        return true;
    return false;
  }
  bool HasOnePossibleValue() const final {
    for (const ZigRecordField &field : GetFields())
      if (!field.IsComptime() && !field.GetType()->HasOnePossibleValue())
        return false;
    return true;
  }
  bool HasComptimeState() const final {
    if (Marker marker = Mark())
      for (const ZigRecordField &field : GetFields())
        if (field.IsComptime() || field.GetType()->HasComptimeState())
          return true;
    return false;
  }

protected:
  ZigRecordType(TypeSystemZig *type_system, Kind kind, ConstString name,
                uint64_t byte_size, llvm::Align align);

  void PrintName(Stream &s) const final { llvm_unreachable("missing name"); }

private:
  friend TypeSystemZig;

  void UpdateFields() const;

  ZigContainer m_container;
  llvm::ArrayRef<ZigRecordField> m_fields;
};

class ZigStructType : public ZigRecordType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstStructType &&
           value->GetKind() <= Kind::LastStructType;
  }

  ZigStructType(TypeSystemZig *type_system, ConstString name,
                uint64_t byte_size, llvm::Align align);

protected:
  ZigStructType(TypeSystemZig *type_system, Kind kind, ConstString name,
                uint64_t byte_size, llvm::Align align);
};

class ZigPackedStructType final : public ZigStructType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::PackedStructType;
  }

  ZigPackedStructType(TypeSystemZig *type_system, ConstString name,
                      ZigIntType *backing_type);

  ZigIntType *GetBackingType() const { return m_backing_type; }
  uint64_t GetBitSize() const final { return GetBackingType()->GetBitSize(); }

private:
  ZigIntType *m_backing_type;
};

class ZigUnionType : public ZigRecordType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() >= Kind::FirstUnionType &&
           value->GetKind() <= Kind::LastUnionType;
  }

  ZigUnionType(TypeSystemZig *type_system, ConstString name, uint64_t byte_size,
               llvm::Align align);

protected:
  ZigUnionType(TypeSystemZig *type_system, Kind kind, ConstString name,
               uint64_t byte_size, llvm::Align align);
};

class ZigTaggedUnionType final : public ZigUnionType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::TaggedUnionType;
  }

  ZigTaggedUnionType(TypeSystemZig *type_system, ConstString name,
                     ZigTagType *tag_type, uint32_t tag_byte_offset,
                     uint64_t byte_size, llvm::Align align);

  ZigTagType *GetTagType() { return m_tag_type; }
  uint64_t GetTagBitOffset() const { return m_tag_byte_offset * UINT64_C(8); }

private:
  ZigTagType *m_tag_type;
  uint32_t m_tag_byte_offset;
};

class ZigPackedUnionType final : public ZigUnionType {
public:
  static bool classof(const ZigValue *value) {
    return value->GetKind() == Kind::PackedUnionType;
  }

  ZigPackedUnionType(TypeSystemZig *type_system, ConstString name,
                     ZigIntType *backing_type);

  ZigIntType *GetBackingType() const { return m_backing_type; }
  uint64_t GetBitSize() const final { return GetBackingType()->GetBitSize(); }

private:
  ZigIntType *m_backing_type;
};

inline TypeSystemZig *ZigValue::GetTypeSystem() const {
  return GetType()->GetTypeSystem();
}

inline TypeSystemZig *ZigType::GetTypeSystem() const {
  return GetType()->GetTypeSystem();
}

inline ConstString ZigType::GetQualifiedName() const {
  if (const ZigNamespace *zig_namespace = GetNamespace())
    return zig_namespace->GetQualifiedName();
  return GetName();
}

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_ZIGVALUE_H
