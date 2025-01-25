//===-- TypeSystemZig.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_TYPESYSTEMZIG_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_TYPESYSTEMZIG_H

#include "ZigValue.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Allocator.h"

#include <cstdint>
#include <memory>
#include <optional>

class DWARFASTParserZig;

namespace clang {
class TargetInfo;
}

namespace lldb_private {

/// A TypeSystem implementation for the Zig Programming Language.
class TypeSystemZig : public TypeSystem {
  // LLVM RTTI support
  static char ID;

public:
  // llvm casting support
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  explicit TypeSystemZig(llvm::StringRef name, ArchSpec arch);

  ~TypeSystemZig() override;

  // PluginInterface functions
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  static llvm::StringRef GetPluginNameStatic() { return "zig"; }

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module, Target *target);

  static void Initialize();

  static void Terminate();

  llvm::StringRef GetDisplayName() const { return m_display_name; }

  const clang::TargetInfo *GetTargetInfo();

  ZigModule *GetModule(ConstString name);

  ZigConstant *GetConstant(void *opaque_decl_ctx, ConstString name,
                           ZigValue *value);
  ZigAlias *GetAlias(void *opaque_decl_ctx, ConstString name, ZigType *value);
  ZigVariable *GetVariable(void *opaque_decl_ctx, ConstString name,
                           lldb::opaque_compiler_type_t type);
  ZigFunction *GetFunction(void *opaque_decl_ctx, ConstString name,
                           lldb::opaque_compiler_type_t type);
  ZigBlock *GetBlock(void *opaque_decl_ctx);

  ZigValue *GetData(ZigType *type, llvm::ArrayRef<uint8_t>);
  ZigOnlyPossibleValue *GetOnlyPossibleValue(ZigType *type);
  ZigOnlyPossibleValue *GetVoid();
  ZigComptimeInt *GetComptimeInt(const llvm::APSInt &value);
  ZigComptimeFloat *GetComptimeFloat(const llvm::APFloat &value);
  ZigOnlyPossibleValue *GetUndefined();
  ZigOnlyPossibleValue *GetNull();
  ZigEnumLiteral *GetEnumLiteral(ConstString value);
  ZigBool *GetBool(bool value);
  ZigInt *GetInt(ZigIntType *type, const llvm::APInt &value);
  ZigFloat *GetFloat(ZigFloatType *type, const llvm::APFloat &value);
  ZigPointer *GetPointer(ZigPointerType *type, ZigValue *pointee,
                         uint64_t offset);
  ZigSlice *GetSlice(ZigPointerType *type, ZigValue *pointee, uint64_t offset,
                     uint64_t len);
  ZigTag *GetTag(ZigTagType *type, const llvm::APInt &value);

  ZigTypeType *GetZigTypeType();
  CompilerType GetTypeType();
  CompilerType GetVoidType();
  CompilerType GetNoReturnType();
  CompilerType GetComptimeIntType();
  CompilerType GetComptimeFloatType();
  CompilerType GetUndefinedType();
  CompilerType GetNullType();
  CompilerType GetAnyOpaqueType();
  CompilerType GetEnumLiteralType();
  CompilerType GetAnyType();
  CompilerType GetSizeType(bool is_signed);

  CompilerType GetBoolType();
  CompilerType GetIntType(ConstString name, bool is_signed, uint16_t bit_size,
                          uint16_t byte_size, llvm::MaybeAlign align);
  CompilerType GetFloatType(ConstString name, uint16_t bit_size,
                            uint16_t byte_size, llvm::MaybeAlign align);

  CompilerType GetOptionalType(lldb::opaque_compiler_type_t child);

  CompilerType GetPointerType(ZigPointerType::Size size, ZigValue *sentinel,
                              bool is_allowzero, llvm::MaybeAlign pointer_align,
                              ZigPointerType::AddressSpace addrspace,
                              bool is_const, bool is_volatile,
                              lldb::opaque_compiler_type_t child);

  CompilerType GetErrorSetType(ConstString name,
                               lldb::opaque_compiler_type_t backing_int_type,
                               llvm::ArrayRef<ZigTagField> errors);
  CompilerType GetErrorUnionType(lldb::opaque_compiler_type_t error_set_type,
                                 lldb::opaque_compiler_type_t payload_type);

  CompilerType GetArrayType(uint64_t size, ZigValue *sentinel,
                            lldb::opaque_compiler_type_t type);
  CompilerType GetVectorType(uint32_t size, lldb::opaque_compiler_type_t type);

  CompilerType GetFunctionType(llvm::ArrayRef<ZigType *> param_types,
                               bool is_var_args,
                               lldb::opaque_compiler_type_t ret_type);

  CompilerType
  GetGeneratedTagType(ConstString name,
                      lldb::opaque_compiler_type_t backing_int_type,
                      llvm::ArrayRef<ZigTagField> fields);

  CompilerType GetEnumType(ConstString name,
                           lldb::opaque_compiler_type_t backing_int_type,
                           llvm::ArrayRef<ZigTagField> fields);
  CompilerType GetTupleType(llvm::ArrayRef<ZigTupleField> fields,
                            uint64_t byte_size, llvm::Align align);
  CompilerType GetStructType(ConstString name, uint64_t byte_size,
                             llvm::Align align);
  CompilerType
  GetPackedStructType(ConstString name,
                      lldb::opaque_compiler_type_t backing_int_type);
  CompilerType GetUnionType(ConstString name, uint64_t byte_size,
                            llvm::Align align);
  CompilerType GetTaggedUnionType(ConstString name,
                                  lldb::opaque_compiler_type_t tag_type,
                                  uint32_t tag_byte_offset, uint64_t byte_size,
                                  llvm::Align align);
  CompilerType
  GetPackedUnionType(ConstString name,
                     lldb::opaque_compiler_type_t backing_int_type);

  // Basic Types
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override;

  CompilerDeclContext
  GetCompilerDeclContextForType(const CompilerType &type) override;

  lldb::ByteOrder GetByteOrder();

  uint32_t GetPointerByteSize() override;

  // TypeSystem methods
  plugin::dwarf::DWARFASTParser *GetDWARFParser() override;

  // CompilerDecl override functions
  ConstString DeclGetName(void *opaque_decl) override;

  CompilerDeclContext DeclGetDeclContext(void *opaque_decl) override;

  CompilerType DeclGetFunctionReturnType(void *opaque_decl) override;

  size_t DeclGetFunctionNumArguments(void *opaque_decl) override;

  CompilerType DeclGetFunctionArgumentType(void *opaque_decl,
                                           size_t arg_idx) override;

  std::vector<CompilerContext>
  DeclGetCompilerContext(void *opaque_decl) override;

  Scalar DeclGetConstantValue(void *opaque_decl) override;

  lldb::ValueObjectSP
  DeclGetConstantValueObject(void *opaque_decl,
                             ExecutionContextScope *exe_scope);

  CompilerType GetTypeForDecl(void *opaque_decl) override;

  // CompilerDeclContext override functions

  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            bool ignore_using_decls) override;

  ConstString DeclContextGetName(void *opaque_decl_ctx) override;

  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;

  bool DeclContextIsClassMethod(void *opaque_decl_ctx) override;

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override;

  lldb::LanguageType DeclContextGetLanguage(void *opaque_decl_ctx) override;

  // Tests

#ifndef NDEBUG
  bool VerifyDeclContext(void *opaque_decl_ctx);
  bool VerifyDecl(void *opaque_decl);
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;

  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;

  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;

  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;

  bool IsCharType(lldb::opaque_compiler_type_t type) override;

  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;

  bool IsConst(lldb::opaque_compiler_type_t type) override;

  bool IsDefined(lldb::opaque_compiler_type_t type) override;

  bool IsFloatingPointType(lldb::opaque_compiler_type_t type, uint32_t &count,
                           bool &is_complex) override;

  unsigned GetPtrAuthKey(lldb::opaque_compiler_type_t type) override;
  unsigned GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) override;
  bool GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t type) override;

  bool IsFunctionType(lldb::opaque_compiler_type_t type) override;

  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type) override;

  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t index) override;

  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;

  bool IsMemberFunctionPointerType(lldb::opaque_compiler_type_t type) override;

  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type) override;

  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;

  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;

  bool IsScopedEnumerationType(lldb::opaque_compiler_type_t type) override {
    bool is_signed;
    return IsEnumerationType(type, is_signed);
  }

  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;

  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, // Can pass nullptr
                             bool check_cplusplus, bool check_objc) override;

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;

  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override {
    return IsPointerType(type, pointee_type);
  }

  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override {
    if (pointee_type)
      pointee_type->Clear();
    if (is_rvalue)
      *is_rvalue = false;
    return false;
  }

  bool IsScalarType(lldb::opaque_compiler_type_t type) override;

  bool IsTypedefType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsVoidType(lldb::opaque_compiler_type_t type) override;

  bool CanPassInRegisters(const CompilerType &type) override;

  bool SupportsLanguage(lldb::LanguageType language) override;

  // Type Completion

  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;

  // Accessors

  ConstString GetTypeName(lldb::opaque_compiler_type_t type,
                          bool base_only) override;

  ConstString GetDisplayTypeName(lldb::opaque_compiler_type_t type) override;

  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;

  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;

  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;

  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;

  // Creating related types

  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) override;

  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override {
    return GetArrayType(size, nullptr, type);
  }

  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;

  CompilerType
  GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) override;

  // Returns -1 if this isn't a function of if the function doesn't have a
  // prototype Returns a value >= 0 if there is a prototype.
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;

  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;

  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;

  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;

  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;

  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;

  // If the current object represents a typedef type, get the underlying type
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;

  // Create related types using the current type's AST
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;

  // Exploring the type

  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size) override;

  std::optional<uint64_t>
  GetByteSize(lldb::opaque_compiler_type_t type,
              ExecutionContextScope *exe_scope) override;

  std::optional<uint64_t> GetBitSize(lldb::opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) override;

  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                             uint64_t &count) override;

  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;

  std::optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override;

  llvm::Expected<uint32_t>
  GetNumChildren(lldb::opaque_compiler_type_t type,
                 bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) override;

  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;

  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &int_type, ConstString name,
                         const llvm::APSInt &value)> const &callback) override;

  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;

  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset,
                               uint32_t *bitfield_bit_size,
                               bool *is_bitfield) override;

  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;

  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;

  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset) override;

  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset) override;

  CompilerDecl GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                      llvm::StringRef name) override;

  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint64_t &child_bit_size, int64_t &child_bit_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;

  // Lookup a child given a name. This function will match base class names and
  // member member names in "clang_type" only, not descendants.
  uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                   llvm::StringRef name,
                                   bool omit_empty_base_classes) override;

  // Lookup a child member given a name. This function will match member names
  // only and will descend into "clang_type" children in search for the first
  // member in this class, or any base class that matches "name".
  // TODO: Return all matches for a given name by returning a
  // vector<vector<uint32_t>>
  // so we catch all names that match a given child name, not just the first.
  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                llvm::StringRef name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indices) override;

  ValueObject *GetStringPointer(lldb::opaque_compiler_type_t type,
                                ValueObject *valobj, uint64_t *length,
                                char *terminator) override;

  CompilerType GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) override;

  lldb::ValueObjectSP
  CreateValueFromType(lldb::opaque_compiler_type_t type,
                      ExecutionContextScope *exe_scope) override;

  static void PrintIdentifier(llvm::StringRef id, Stream &s);

  static ConstString ChildFieldName(llvm::StringRef name);

  bool DumpTagValue(ZigTagType *type, Stream &s, llvm::APSInt value);

  bool DumpValue(ZigValue *value, Stream &s);

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream &s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t byte_offset, size_t byte_size,
                     uint32_t bit_size, uint32_t bit_offset,
                     ExecutionContextScope *exe_scope) override;

  bool DumpTypeDecl(lldb::opaque_compiler_type_t type, Stream &s);

  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;

  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type, Stream &s,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;

  // Dumping types
#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  /// In contrast to the other \p Dump() methods this directly invokes
  /// \p clang::QualType::dump().
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override;
#endif

  /// \see TypeSystem::Dump
  void Dump(llvm::raw_ostream &output) override;

  CompilerDeclContext WrapScope(ZigScope *zig_scope = nullptr);
  ZigScope *UnwrapScope(void *opaque_decl_ctx);
  ZigScope *UnwrapScope(CompilerDeclContext decl_ctx);
  ZigScope *UnwrapScope(ZigScope *) = delete;
  ZigScope *UnwrapScope(ZigValue *) = delete;

  CompilerDecl WrapDecl(ZigDeclaration *zig_decl = nullptr);
  ZigDeclaration *UnwrapDecl(void *opaque_decl);
  ZigDeclaration *UnwrapDecl(CompilerDecl decl);
  ZigDeclaration *UnwrapDecl(ZigScope *) = delete;
  ZigDeclaration *UnwrapDecl(ZigValue *) = delete;

  CompilerType WrapType(ZigType *zig_type = nullptr);
  ZigType *UnwrapType(lldb::opaque_compiler_type_t type);
  ZigType *UnwrapType(CompilerType type);
  ZigType *UnwrapType(ZigScope *) = delete;
  ZigType *UnwrapType(ZigValue *) = delete;

  void TypeUpdateParent(lldb::opaque_compiler_type_t type);
  void SetParentScope(lldb::opaque_compiler_type_t type, void *opaque_decl_ctx);
  void SetFields(lldb::opaque_compiler_type_t type,
                 llvm::ArrayRef<ZigRecordField> fields);

private:
  llvm::APSInt LoadTargetInt(ZigType *type, llvm::ArrayRef<uint8_t> bytes,
                             std::optional<uint32_t> bit_offset = std::nullopt);

  ArchSpec m_arch;
  std::unique_ptr<clang::TargetInfo> m_target_info_up;
  llvm::BumpPtrAllocator m_allocator;
  struct ZigDataKeyInfo;
  llvm::DenseSet<ZigData *, ZigDataKeyInfo> m_data;
  struct ZigOnlyPossibleValueKeyInfo;
  llvm::DenseSet<ZigOnlyPossibleValue *, ZigOnlyPossibleValueKeyInfo>
      m_only_possible_values;
  struct ZigComptimeIntKeyInfo;
  llvm::DenseSet<ZigComptimeInt *, ZigComptimeIntKeyInfo> m_comptime_ints;
  struct ZigComptimeFloatKeyInfo;
  llvm::DenseSet<ZigComptimeFloat *, ZigComptimeFloatKeyInfo> m_comptime_floats;
  struct ZigEnumLiteralKeyInfo;
  llvm::DenseSet<ZigEnumLiteral *, ZigEnumLiteralKeyInfo> m_enum_literals;
  ZigBool *m_false = nullptr;
  ZigBool *m_true = nullptr;
  struct ZigIntKeyInfo;
  llvm::DenseSet<ZigInt *, ZigIntKeyInfo> m_ints;
  struct ZigFloatKeyInfo;
  llvm::DenseSet<ZigFloat *, ZigFloatKeyInfo> m_floats;
  struct ZigPointerKeyInfo;
  llvm::DenseSet<ZigPointer *, ZigPointerKeyInfo> m_pointers;
  struct ZigSliceKeyInfo;
  llvm::DenseSet<ZigSlice *, ZigSliceKeyInfo> m_slices;
  struct ZigTagKeyInfo;
  llvm::DenseSet<ZigTag *, ZigTagKeyInfo> m_tags;
  ZigTypeType *m_type_type = nullptr;
  ZigVoidType *m_void_type = nullptr;
  ZigNoReturnType *m_no_return_type = nullptr;
  ZigComptimeFloatType *m_comptime_float_type = nullptr;
  ZigComptimeIntType *m_comptime_int_type = nullptr;
  ZigUndefinedType *m_undefined_type = nullptr;
  ZigNullType *m_null_type = nullptr;
  ZigAnyOpaqueType *m_any_opaque_type = nullptr;
  ZigEnumLiteralType *m_enum_literal_type = nullptr;
  ZigAnyType *m_any_type = nullptr;
  ZigBoolType *m_bool_type = nullptr;
  struct ZigNamedIntTypeKeyInfo;
  llvm::DenseSet<ZigIntType *, ZigNamedIntTypeKeyInfo> m_named_int_types;
  struct ZigIntTypeKeyInfo;
  llvm::DenseSet<ZigIntType *, ZigIntTypeKeyInfo> m_int_types;
  ZigFloatType *m_f16_type = nullptr;
  ZigFloatType *m_f32_type = nullptr;
  ZigFloatType *m_f64_type = nullptr;
  ZigFloatType *m_f80_type = nullptr;
  ZigFloatType *m_f128_type = nullptr;
  ZigFloatType *m_c_longdouble_type = nullptr;
  struct ZigOptionalTypeKeyInfo;
  llvm::DenseSet<ZigOptionalType *, ZigOptionalTypeKeyInfo> m_optional_types;
  struct ZigPointerTypeKeyInfo;
  llvm::DenseSet<ZigPointerType *, ZigPointerTypeKeyInfo> m_pointer_types;
  struct ZigErrorUnionTypeKeyInfo;
  llvm::DenseSet<ZigErrorUnionType *, ZigErrorUnionTypeKeyInfo>
      m_error_union_types;
  struct ZigArrayTypeKeyInfo;
  llvm::DenseSet<ZigArrayType *, ZigArrayTypeKeyInfo> m_array_types;
  struct ZigVectorTypeKeyInfo;
  llvm::DenseSet<ZigVectorType *, ZigVectorTypeKeyInfo> m_vector_types;
  struct ZigFunctionTypeKeyInfo;
  llvm::DenseSet<ZigFunctionType *, ZigFunctionTypeKeyInfo> m_function_types;
  struct ZigTupleTypeKeyInfo;
  llvm::DenseSet<ZigTupleType *, ZigTupleTypeKeyInfo> m_tuple_types;
  std::unique_ptr<DWARFASTParserZig> m_dwarf_ast_parser_up;
  /// A string describing what this TypeSystemZig represents (e.g.,
  /// AST for debug information, an expression).
  /// Useful for logging and debugging.
  std::string m_display_name;
};

/// The TypeSystemZig instance used for the scratch ASTContext in a
/// lldb::Target.
class ScratchTypeSystemZig : public TypeSystemZig {
  /// LLVM RTTI support
  static char ID;

public:
  // llvm casting support
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemZig::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  ScratchTypeSystemZig(Target &target);

  ~ScratchTypeSystemZig() override = default;

  UserExpression *GetUserExpression(llvm::StringRef expr,
                                    llvm::StringRef prefix,
                                    SourceLanguage language,
                                    Expression::ResultType desired_type,
                                    const EvaluateExpressionOptions &options,
                                    ValueObject *ctx_obj) override;

  /// \see TypeSystem::Dump
  void Dump(llvm::raw_ostream &output) override;

private:
  lldb::TargetWP m_target_wp;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_ZIG_TYPESYSTEMZIG_H
