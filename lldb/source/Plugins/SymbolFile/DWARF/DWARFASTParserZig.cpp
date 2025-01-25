//===-- DWARFASTParserZig.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFASTParserZig.h"

#include "DWARFAttribute.h"
#include "DWARFDIE.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"

#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Utility/Endian.h"

#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FormatAdapters.h"

#include <utility>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

DWARFASTParserZig::DWARFASTParserZig(TypeSystemZig &ast)
    : DWARFASTParser(Kind::DWARFASTParserZig), m_ast(ast) {
  m_parent_to_children_indices.push_back(m_parent_to_children.size());
}

DWARFASTParserZig::~DWARFASTParserZig() = default;

ParsedDWARFTypeAttributesZig::ParsedDWARFTypeAttributesZig(
    const DWARFDIE &die) {
  DWARFAttributes attributes = die.GetAttributes();
  for (size_t i = 0; i < attributes.Size(); ++i)
    if (DWARFFormValue form_value;
        attributes.ExtractFormValueAtIndex(i, form_value))
      switch (attributes.AttributeAtIndex(i)) {
      default:
        break;
      case DW_AT_name:
        name.SetCString(form_value.AsCString());
        break;
      case DW_AT_decl_file:
        // die.GetCU() can differ if DW_AT_specification uses DW_FORM_ref_addr.
        decl.SetFile(
            attributes.CompileUnitAtIndex(i)->GetFile(form_value.Unsigned()));
        break;
      case DW_AT_decl_line:
        decl.SetLine(form_value.Unsigned());
        break;
      case DW_AT_decl_column:
        decl.SetColumn(form_value.Unsigned());
        break;
      case DW_AT_abstract_origin:
        abstract_origin = form_value;
        break;
      case DW_AT_signature:
        signature = form_value;
        break;
      case DW_AT_type:
        type = form_value;
        break;
      case DW_AT_ZIG_sentinel:
        sentinel = form_value;
        break;
      case DW_AT_bit_size:
        bit_size = form_value.Unsigned();
        break;
      case DW_AT_byte_size:
        byte_size = form_value.Unsigned();
        break;
      case DW_AT_alignment:
        align = llvm::Align(form_value.Unsigned());
        break;
      case DW_AT_encoding:
        encoding = form_value.Unsigned();
        break;
      case DW_AT_GNU_vector:
        is_vector = form_value.Boolean();
        break;
      case DW_AT_address_class:
        address_class = form_value.Unsigned();
        break;
      }
}

TypeSP DWARFASTParserZig::ParseTypeFromDWARF(const SymbolContext &sc,
                                             const DWARFDIE &die,
                                             bool *type_is_new_ptr) {
  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  if (!die)
    return nullptr;

  // Set a bit that lets us know that we are currently parsing this
  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (auto [it, inserted] =
          dwarf->GetDIEToType().try_emplace(die.GetDIE(), DIE_IS_BEING_PARSED);
      !inserted) {
    if (it->getSecond() == nullptr || it->getSecond() == DIE_IS_BEING_PARSED)
      return nullptr;
    return it->getSecond()->shared_from_this();
  }

  ParsedDWARFTypeAttributesZig attrs(die);

  TypeSP type_sp;
  if (DWARFDIE signature_die = attrs.signature.Reference()) {
    type_sp = ParseTypeFromDWARF(sc, signature_die, type_is_new_ptr);
  } else {
    if (type_is_new_ptr)
      *type_is_new_ptr = true;

    ModuleSP module_sp = dwarf->GetObjectFile()->GetModule();
    lldb::user_id_t encoding_uid = LLDB_INVALID_UID;
    Type::EncodingDataType encoding_uid_type = Type::eEncodingIsUID;
    CompilerType zig_type;
    ZigScope *zig_scope = nullptr;
    bool is_forward = false;
    const dw_tag_t tag = die.Tag();
    switch (tag) {
    case DW_TAG_const_type:
    case DW_TAG_volatile_type: {
      DWARFDIE type_die = attrs.type.Reference();
      if (Type *type = type_die.ResolveType()) {
        encoding_uid = type_die.GetID();
        switch (tag) {
        case DW_TAG_const_type:
          encoding_uid_type = Type::eEncodingIsConstUID;
          break;
        case DW_TAG_volatile_type:
          encoding_uid_type = Type::eEncodingIsVolatileUID;
          break;
        default:
          llvm_unreachable("already checked");
        }
        zig_type = type->GetForwardCompilerType();
      }
      break;
    }
    case DW_TAG_typedef:
      if (Type *ref_type = attrs.type.Reference().ResolveType()) {
        llvm::StringRef name(attrs.name);
        if (name.starts_with("@typeInfo(@typeInfo(@TypeOf(") &&
            name.ends_with(")).@\"fn\".return_type.?).error_union.error_set"))
          zig_type = ref_type->GetForwardCompilerType();
      }
      break;
    case DW_TAG_unspecified_type:
      if (attrs.name == "type")
        zig_type = m_ast.GetTypeType();
      else if (attrs.name == "void")
        zig_type = m_ast.GetVoidType();
      else if (attrs.name == "noreturn")
        zig_type = m_ast.GetNoReturnType();
      else if (attrs.name == "comptime_float")
        zig_type = m_ast.GetComptimeFloatType();
      else if (attrs.name == "comptime_int")
        zig_type = m_ast.GetComptimeIntType();
      else if (attrs.name == "@TypeOf(undefined)")
        zig_type = m_ast.GetUndefinedType();
      else if (attrs.name == "@TypeOf(null)")
        zig_type = m_ast.GetNullType();
      else if (attrs.name == "anyopaque")
        zig_type = m_ast.GetAnyOpaqueType();
      else if (attrs.name == "@Type(.enum_literal)")
        zig_type = m_ast.GetEnumLiteralType();
      else if (attrs.name == "anytype")
        zig_type = m_ast.GetAnyType();
      break;
    case DW_TAG_base_type:
      switch (attrs.encoding) {
      case DW_ATE_boolean:
        zig_type = m_ast.GetBoolType();
        break;
      case DW_ATE_signed:
      case DW_ATE_unsigned:
        zig_type = m_ast.GetIntType(attrs.name, attrs.encoding == DW_ATE_signed,
                                    attrs.bit_size.value_or(0),
                                    attrs.byte_size.value_or(0), attrs.align);
        break;
      case DW_ATE_float:
        zig_type = m_ast.GetFloatType(attrs.name, attrs.bit_size.value_or(0),
                                      attrs.byte_size.value_or(0), attrs.align);
        break;
      default:
        module_sp->ReportError(
            "[{0:x16}]: unhandled encoding {1:x2}, please file a bug and "
            "attach the file at the start of this error message",
            die.GetOffset(), attrs.encoding);
        return nullptr;
      }
      break;
    case DW_TAG_pointer_type: {
      if (Type *child_type = attrs.type.Reference().ResolveType()) {
        ZigType *zig_child_type =
            m_ast.UnwrapType(child_type->GetForwardCompilerType());
        uint32_t encoding_mask = child_type->GetEncodingMask();
        ZigPointerType::Size size;
        ZigValue *sentinel = nullptr;
        llvm::StringRef name(attrs.name);
        if (name.consume_front("[*")) {
          if (name.consume_front("c")) {
            size = ZigPointerType::Size::C;
            if (attrs.sentinel.IsValid())
              break;
          } else {
            size = ZigPointerType::Size::Many;
            bool has_sentinel = name.consume_front(":");
            if (has_sentinel != attrs.sentinel.IsValid())
              break;
            if (has_sentinel) {
              name = name.substr(name.find(']'));
              sentinel = ParseValue(attrs.sentinel, zig_child_type);
              if (!sentinel)
                break;
            }
          }
          if (!name.consume_front("]"))
            break;
        } else if (name.consume_front("*")) {
          size = ZigPointerType::Size::One;
          if (attrs.sentinel.IsValid())
            break;
        } else
          break;
        bool is_allowzero =
            size == ZigPointerType::Size::C || name.consume_front("allowzero ");
        if (name.consume_front("align("))
          name = name.substr(name.find(") ")).substr(2);
        if (name.consume_front("addrspace("))
          name = name.substr(name.find(") ")).substr(2);
        bool is_const = name.consume_front("const ");
        if (is_const != (encoding_mask >> Type::eEncodingIsConstUID & 1u))
          break;
        bool is_volatile = name.consume_front("volatile ");
        if (is_volatile != (encoding_mask >> Type::eEncodingIsVolatileUID & 1u))
          break;
        zig_type = m_ast.GetPointerType(
            size, sentinel, is_allowzero, attrs.align,
            static_cast<ZigPointerType::AddressSpace>(attrs.address_class),
            is_const, is_volatile, zig_child_type);
      }
      break;
    }
    case DW_TAG_array_type: {
      Type *child_type = attrs.type.Reference().ResolveType();
      DWARFDIE subrange = die.GetFirstChild();
      if (child_type && subrange.Tag() == DW_TAG_subrange_type) {
        ZigType *zig_child_type =
            m_ast.UnwrapType(child_type->GetForwardCompilerType());
        uint64_t len = subrange.GetAttributeValueAsUnsigned(DW_AT_count, 0);
        if (attrs.is_vector)
          zig_type = m_ast.GetVectorType(len, zig_child_type);
        else {
          ZigValue *sentinel = ParseValue(attrs.sentinel, zig_child_type);
          if ((sentinel != nullptr) != attrs.sentinel.IsValid())
            break;
          zig_type = m_ast.GetArrayType(len, sentinel, zig_child_type);
        }
      }
      break;
    }
    case DW_TAG_enumeration_type:
      if (Type *backing_type = attrs.type.Reference().ResolveType()) {
        if (ZigIntType *zig_backing_type =
                llvm::dyn_cast_if_present<ZigIntType>(
                    m_ast.UnwrapType(backing_type->GetForwardCompilerType()))) {
          uint16_t bit_size = zig_backing_type->GetBitSize();
          llvm::SmallVector<ZigTagField, 16> fields;
          for (DWARFDIE child_die : die.children()) {
            if (child_die.Tag() != DW_TAG_enumerator)
              continue;
            if (std::optional<uint64_t> value =
                    child_die.GetAttributeValueAsOptionalUnsigned(
                        DW_AT_const_value))
              fields.emplace_back(
                  ConstString(child_die.GetName()),
                  m_ast.GetInt(zig_backing_type,
                               llvm::APInt(bit_size, *value,
                                           zig_backing_type->IsSigned())));
          }
          llvm::StringRef name(attrs.name);
          if (name == "anyerror" ||
              (name.starts_with("error{") && name.ends_with('}')))
            zig_type =
                m_ast.GetErrorSetType(attrs.name, zig_backing_type, fields);
          else if (name.starts_with("@typeInfo(") &&
                   name.ends_with(").@\"union\".tag_type.?"))
            zig_type =
                m_ast.GetGeneratedTagType(attrs.name, zig_backing_type, fields);
          else
            zig_type = m_ast.GetEnumType(attrs.name, zig_backing_type, fields);
          is_forward = true;
        }
      }
      break;
    case DW_TAG_structure_type:
      if (DWARFDIE first_member_die = die.GetFirstChild();
          first_member_die.Tag() == DW_TAG_member) {
        const MemberAttributes first_member_attrs(first_member_die);
        if (first_member_attrs.is_artificial) {
          if (first_member_attrs.name == "ptr") {
            if (Type *many_ptr_type =
                    first_member_attrs.type.Reference().ResolveType())
              if (ZigPointerType *zig_many_ptr_type =
                      llvm::dyn_cast<ZigPointerType>(m_ast.UnwrapType(
                          many_ptr_type->GetForwardCompilerType())))
                if (zig_many_ptr_type->GetSize() == ZigPointerType::Size::Many)
                  zig_type =
                      m_ast.GetPointerType(ZigPointerType::Size::Slice,
                                           zig_many_ptr_type->GetSentinel(),
                                           zig_many_ptr_type->IsAllowZero(),
                                           zig_many_ptr_type->GetPointerAlign(),
                                           zig_many_ptr_type->GetAddressSpace(),
                                           zig_many_ptr_type->IsConst(),
                                           zig_many_ptr_type->IsVolatile(),
                                           zig_many_ptr_type->GetChildType());
          }
          if (zig_type) {
            for (DWARFDIE member_die : die.children()) {
              if (member_die.Tag() != DW_TAG_member)
                continue;
              const MemberAttributes member_attrs(member_die);
              if (Type *member_type =
                      member_attrs.type.Reference().ResolveType())
                member_type->GetLayoutCompilerType().GetCompleteType();
            }
            break;
          }
        }
      }
      if (Type *backing_type = attrs.type.Reference().ResolveType()) {
        zig_type = m_ast.GetPackedStructType(
            attrs.name,
            backing_type->GetForwardCompilerType().GetOpaqueQualType());
        is_forward = true;
      } else if (attrs.name.GetStringRef().starts_with("struct {")) {
        llvm::SmallVector<ZigTupleField, 8> fields;
        uint32_t byte_offset = 0;
        for (DWARFDIE member_die : die.children()) {
          if (member_die.Tag() != DW_TAG_member)
            continue;
          const MemberAttributes member_attrs(member_die);
          Type *member_type = member_attrs.type.Reference().ResolveType();
          if (!member_type) {
            if (member_attrs.name)
              module_sp->ReportError(
                  "{0:x8}: DW_TAG_member '{1}' refers to type {2:x16}"
                  " which was unable to be parsed",
                  uint32_t(die.GetID()), member_attrs.name,
                  member_attrs.type.Reference().GetOffset());
            else
              module_sp->ReportError(
                  "{0:x8}: DW_TAG_member refers to type {1:x16}"
                  " which was unable to be parsed",
                  uint32_t(die.GetID()),
                  member_attrs.type.Reference().GetOffset());
            return nullptr;
          }
          CompilerType member_compiler_type =
              member_type->GetLayoutCompilerType();
          member_compiler_type.GetCompleteType();
          ZigType *member_zig_type = m_ast.UnwrapType(member_compiler_type);
          llvm::Align member_zig_align = member_zig_type->GetAlign();
          if (member_attrs.align && *member_attrs.align != member_zig_align)
            return nullptr;
          if (!member_attrs.is_comptime) {
            byte_offset = alignTo(byte_offset, member_zig_align);
            if (!member_attrs.is_comptime &&
                member_attrs.bit_offset != byte_offset * 8)
              return nullptr;
          }
          ZigValue *member_zig_default_value;
          if (!member_attrs.is_comptime)
            member_zig_default_value = nullptr;
          else if (member_attrs.comptime_value.IsValid())
            member_zig_default_value =
                ParseComptimeValue(member_attrs.comptime_value.Reference());
          else if (member_attrs.default_value.IsValid())
            member_zig_default_value =
                ParseValue(member_attrs.default_value, member_zig_type);
          else
            member_zig_default_value =
                m_ast.GetOnlyPossibleValue(member_zig_type);
          if ((member_zig_default_value != nullptr) != member_attrs.is_comptime)
            return nullptr;
          fields.emplace_back(member_zig_type, member_zig_default_value);
          if (!member_attrs.is_comptime)
            byte_offset += member_zig_type->GetByteSize();
        }
        zig_type = m_ast.GetTupleType(fields, attrs.byte_size.value_or(0),
                                      attrs.align.valueOrOne());
      } else {
        zig_type = m_ast.GetStructType(attrs.name, attrs.byte_size.value_or(0),
                                       attrs.align.valueOrOne());
        is_forward = true;
      }
      break;
    case DW_TAG_union_type:
      if (DWARFDIE part_die = die.GetFirstChild();
          part_die.Tag() == DW_TAG_variant_part) {
        if (DWARFDIE discr_die = part_die.GetReferencedDIE(DW_AT_discr);
            discr_die.Tag() == DW_TAG_member) {
          const MemberAttributes discr_attrs(discr_die);
          if (!discr_attrs.is_artificial)
            break;
          if (discr_attrs.name == "tag") {
            if (Type *tag_type = discr_attrs.type.Reference().ResolveType()) {
              zig_type = m_ast.GetTaggedUnionType(
                  attrs.name,
                  tag_type->GetLayoutCompilerType().GetOpaqueQualType(),
                  discr_attrs.bit_offset / 8, attrs.byte_size.value_or(0),
                  attrs.align.valueOrOne());
              is_forward = true;
            }
          } else {
            CompilerType opt_child_type, error_type, value_type;
            for (DWARFDIE discr_child_die : part_die.children()) {
              switch (discr_child_die.Tag()) {
              default:
                break;
              case DW_TAG_member:
                if (Type *member_type = GetTypeForDIE(discr_child_die))
                  member_type->GetLayoutCompilerType().GetCompleteType();
                break;
              case DW_TAG_variant:
                for (DWARFDIE variant_child_die : discr_child_die.children()) {
                  if (variant_child_die.Tag() != DW_TAG_member)
                    continue;
                  const MemberAttributes member_attrs(variant_child_die);
                  Type *member_type =
                      member_attrs.type.Reference().ResolveType();
                  if (!member_type)
                    continue;
                  CompilerType member_zig_type =
                      member_type->GetLayoutCompilerType();
                  member_zig_type.GetCompleteType();
                  if (zig_type)
                    continue;
                  std::optional<uint64_t> discr_value =
                      discr_child_die.GetAttributeValueAsOptionalUnsigned(
                          DW_AT_discr_value);
                  if (discr_attrs.name == "has_value" &&
                      member_attrs.name == "?" && discr_value == std::nullopt)
                    opt_child_type = member_zig_type;
                  else if (discr_attrs.name == "is_error" &&
                           member_attrs.name == "error" &&
                           discr_value == std::nullopt)
                    error_type = member_zig_type;
                  else if (discr_attrs.name == "is_error" &&
                           member_attrs.name == "value" && discr_value == 0)
                    value_type = member_zig_type;
                }
                break;
              }
              if (discr_attrs.name == "has_value" && opt_child_type)
                zig_type =
                    m_ast.GetOptionalType(opt_child_type.GetOpaqueQualType());
              else if (discr_attrs.name == "is_error" && value_type &&
                       error_type)
                zig_type =
                    m_ast.GetErrorUnionType(error_type.GetOpaqueQualType(),
                                            value_type.GetOpaqueQualType());
            }
          }
        }
      } else if (Type *backing_type = attrs.type.Reference().ResolveType()) {
        zig_type = m_ast.GetPackedUnionType(
            attrs.name,
            backing_type->GetForwardCompilerType().GetOpaqueQualType());
        is_forward = true;
      } else {
        zig_type = m_ast.GetUnionType(attrs.name, attrs.byte_size.value_or(0),
                                      attrs.align.valueOrOne());
        is_forward = true;
      }
      break;
    case DW_TAG_inlined_subroutine:
    case DW_TAG_subprogram:
    case DW_TAG_subroutine_type: {
      CompilerType zig_ret_type;
      if (Type *ret_type = attrs.type.Reference().ResolveType())
        zig_ret_type = ret_type->GetForwardCompilerType();
      else
        zig_ret_type = m_ast.GetVoidType();

      llvm::SmallVector<ZigType *, 16> param_types;
      bool is_var_args = false;
      bool has_generic_params = false;
      ParseChildParameters(die, true, is_var_args, has_generic_params,
                           param_types);

      zig_type = m_ast.GetFunctionType(param_types, is_var_args,
                                       zig_ret_type.GetOpaqueQualType());
      if (attrs.name && tag != DW_TAG_subroutine_type) {
        if (CompilerDeclContext parent =
                GetDeclContextContainingUIDFromDWARF(die))
          zig_scope =
              m_ast.GetFunction(parent.GetOpaqueDeclContext(), attrs.name,
                                zig_type.GetOpaqueQualType());
      }
      break;
    }
    default:
      break;
    }
    if (!zig_type) {
      module_sp->ReportError(
          "[{0:x16}]: unhandled type tag {1:x4} ({2}), please file a bug and "
          "attach the file at the start of this error message",
          die.GetOffset(), uint16_t(tag), DW_TAG_value_to_name(tag));
      return nullptr;
    }
    type_sp = dwarf->MakeType(die.GetID(), attrs.name, attrs.byte_size, nullptr,
                              encoding_uid, encoding_uid_type, &attrs.decl,
                              zig_type, Type::ResolveState::Full);
    UpdateSymbolContextScopeForType(sc, die, type_sp);

    if (is_forward && type_sp) {
      // Leave this as a forward declaration until we need to know
      // the details of the type. lldb_private::Type will
      // automatically call the SymbolFile virtual function
      // "SymbolFileDWARF::CompleteType(Type *)" When the definition
      // needs to be defined.
      bool inserted =
          dwarf->GetForwardDeclCompilerTypeToDIE()
              .try_emplace(zig_type.GetOpaqueQualType(), *die.GetDIERef())
              .second;
      (void)inserted;
      assert(inserted && "Type already in the forward declaration map!");

      assert(!zig_scope && "forward scopes are handled here");
      zig_scope =
          m_ast.UnwrapScope(m_ast.GetCompilerDeclContextForType(zig_type));
    }
    LinkScopeToDIE(zig_scope, die);
  }
  if (type_sp)
    dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
  return type_sp;
}

ConstString
DWARFASTParserZig::ConstructDemangledNameFromDWARF(const DWARFDIE &die) {
  llvm_unreachable("unimplemented");
}

Function *DWARFASTParserZig::ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                                    const DWARFDIE &die,
                                                    AddressRanges func_ranges) {
  llvm::DWARFAddressRangesVector unused_func_ranges;
  const char *name = nullptr;
  const char *mangled = nullptr;
  std::optional<std::pair<DWARFUnit *, size_t>> decl_file;
  std::optional<int> decl_line;
  std::optional<int> decl_column;
  std::optional<std::pair<DWARFUnit *, size_t>> call_file;
  std::optional<int> call_line;
  std::optional<int> call_column;
  DWARFExpressionList frame_base;

  const dw_tag_t tag = die.Tag();

  if (tag != DW_TAG_subprogram)
    return nullptr;

  if (die.GetDIENamesAndRanges(name, mangled, unused_func_ranges, decl_file,
                               decl_line, decl_column, call_file, call_line,
                               call_column, &frame_base)) {
    Mangled func_name;
    if (mangled)
      func_name.SetValue(ConstString(mangled));
    else
      func_name.SetValue(ConstString(name));

    FunctionSP func_sp;
    std::unique_ptr<Declaration> decl_up;
    if (decl_file || decl_line || decl_column)
      decl_up = std::make_unique<Declaration>(
          decl_file ? decl_file->first->GetFile(decl_file->second)
                    : die.GetCU()->GetFile(0),
          decl_line.value_or(0), decl_column.value_or(0));

    // Supply the type _only_ if it has already been parsed
    Type *func_type = die.GetDWARF()->GetDIEToType().lookup(die.GetDIE());

    assert(func_type == nullptr || func_type != DIE_IS_BEING_PARSED);

    const user_id_t func_user_id = die.GetID();

    // The base address of the scope for any of the debugging information
    // entries listed above is given by either the DW_AT_low_pc attribute or the
    // first address in the first range entry in the list of ranges given by the
    // DW_AT_ranges attribute.
    //   -- DWARFv5, Section 2.17 Code Addresses, Ranges and Base Addresses
    //
    // If no DW_AT_entry_pc attribute is present, then the entry address is
    // assumed to be the same as the base address of the containing scope.
    //   -- DWARFv5, Section 2.18 Entry Address
    //
    // We currently don't support Debug Info Entries with
    // DW_AT_low_pc/DW_AT_entry_pc and DW_AT_ranges attributes (the latter
    // attributes are ignored even though they should be used for the address of
    // the function), but compilers also don't emit that kind of information. If
    // this becomes a problem we need to plumb these attributes separately.
    Address func_addr = func_ranges[0].GetBaseAddress();

    func_sp = std::make_shared<Function>(
        &comp_unit,
        func_user_id, // UserID is the DIE offset
        func_user_id, func_name, func_type, std::move(func_addr),
        std::move(func_ranges));

    if (func_sp) {
      if (frame_base.IsValid())
        func_sp->GetFrameBaseExpression() = frame_base;
      comp_unit.AddFunction(func_sp);
      return func_sp.get();
    }
  }
  return nullptr;
}

bool DWARFASTParserZig::CompleteTypeFromDWARF(
    const DWARFDIE &die, Type *type, const CompilerType &compiler_type) {
  if (!die || !type || !compiler_type ||
      compiler_type.GetTypeSystem() != m_ast.shared_from_this())
    return false;

  SymbolFileDWARF *dwarf = die.GetDWARF();
  ModuleSP module_sp = dwarf->GetObjectFile()->GetModule();
  std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());

  // This type has been removed from GetForwardDeclCompilerTypeToDIE, which
  // prevents TypeUpdateParent from updating the parent later, so ensure the
  // parent is up to date now since it is our last chance to do so.
  GetDeclForUIDFromDWARF(die);

  llvm::SmallVector<ZigRecordField, 16> fields;
  ZigType *zig_type = m_ast.UnwrapType(compiler_type);
  switch (zig_type->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
  case ZigValue::Kind::Variable:
  case ZigValue::Kind::Function:
  case ZigValue::Kind::Data:
  case ZigValue::Kind::OnlyPossibleValue:
  case ZigValue::Kind::ComptimeInt:
  case ZigValue::Kind::ComptimeFloat:
  case ZigValue::Kind::EnumLiteral:
  case ZigValue::Kind::BoolFalse:
  case ZigValue::Kind::BoolTrue:
  case ZigValue::Kind::Int:
  case ZigValue::Kind::Float:
  case ZigValue::Kind::Pointer:
  case ZigValue::Kind::Slice:
  case ZigValue::Kind::Tag:
    llvm_unreachable("not a type");
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
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::TupleType:
    llvm_unreachable("not a forward decl");
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    return true;
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::PackedUnionType:
    for (DWARFDIE member_die : die.children()) {
      if (member_die.Tag() != DW_TAG_member)
        continue;
      const MemberAttributes member_attrs(member_die);
      Type *member_type = member_attrs.type.Reference().ResolveType();
      if (!member_type) {
        if (member_attrs.name)
          module_sp->ReportError(
              "{0:x8}: DW_TAG_member '{1}' refers to type {2:x16}"
              " which was unable to be parsed",
              uint32_t(die.GetID()), member_attrs.name,
              member_attrs.type.Reference().GetOffset());
        else
          module_sp->ReportError("{0:x8}: DW_TAG_member refers to type {1:x16}"
                                 " which was unable to be parsed",
                                 uint32_t(die.GetID()),
                                 member_attrs.type.Reference().GetOffset());
        return false;
      }
      CompilerType member_compiler_type = member_type->GetLayoutCompilerType();
      member_compiler_type.GetCompleteType();
      ZigType *member_zig_type = m_ast.UnwrapType(member_compiler_type);
      ZigValue *member_zig_default_value;
      if (!member_attrs.is_comptime)
        member_zig_default_value = nullptr;
      else if (member_attrs.comptime_value.IsValid())
        member_zig_default_value =
            ParseComptimeValue(member_attrs.comptime_value.Reference());
      else if (member_attrs.default_value.IsValid())
        member_zig_default_value =
            ParseValue(member_attrs.default_value, member_zig_type);
      else
        member_zig_default_value = m_ast.GetOnlyPossibleValue(member_zig_type);
      if ((member_zig_default_value != nullptr) != member_attrs.is_comptime)
        return false;
      fields.emplace_back(member_attrs.is_comptime, member_attrs.name,
                          member_zig_type, member_attrs.align,
                          member_zig_default_value, member_attrs.bit_offset);
    }
    break;
  case ZigValue::Kind::TaggedUnionType:
    m_ast.WrapType(llvm::cast<ZigTaggedUnionType>(zig_type)->GetTagType())
        .GetCompleteType();
    DWARFDIE part_die = die.GetFirstChild();
    if (part_die.Tag() != DW_TAG_variant_part)
      return false;
    for (DWARFDIE variant_die : part_die.children()) {
      if (variant_die.Tag() != DW_TAG_variant)
        continue;
      for (DWARFDIE member_die : variant_die.children()) {
        if (member_die.Tag() != DW_TAG_member)
          continue;
        const MemberAttributes member_attrs(member_die);
        Type *member_type = member_attrs.type.Reference().ResolveType();
        if (!member_type) {
          if (member_attrs.name)
            module_sp->ReportError(
                "{0:x8}: DW_TAG_member '{1}' refers to type {2:x16}"
                " which was unable to be parsed",
                uint32_t(die.GetID()), member_attrs.name,
                member_attrs.type.Reference().GetOffset());
          else
            module_sp->ReportError(
                "{0:x8}: DW_TAG_member refers to type {1:x16}"
                " which was unable to be parsed",
                uint32_t(die.GetID()),
                member_attrs.type.Reference().GetOffset());
          return false;
        }
        CompilerType member_zig_type = member_type->GetLayoutCompilerType();
        member_zig_type.GetCompleteType();
        if (member_attrs.is_comptime || member_attrs.default_value.IsValid() ||
            member_attrs.comptime_value.IsValid())
          return false;
        fields.emplace_back(member_attrs.is_comptime, member_attrs.name,
                            m_ast.UnwrapType(member_zig_type),
                            member_attrs.align, nullptr,
                            member_attrs.bit_offset);
        break;
      }
    }
    break;
  }
  m_ast.SetFields(compiler_type.GetOpaqueQualType(), fields);
  return true;
}

CompilerDecl DWARFASTParserZig::GetDeclForUIDFromDWARF(const DWARFDIE &die) {
  if (!die)
    return m_ast.WrapDecl();
  if (auto [it, inserted] = m_die_to_decl.try_emplace(die.GetDIE(), nullptr);
      !inserted)
    return m_ast.WrapDecl(it->second);

  ZigDeclaration *zig_decl = nullptr;
  const dw_tag_t tag = die.Tag();
  switch (tag) {
  case DW_TAG_constant:
  case DW_TAG_formal_parameter:
  case DW_TAG_imported_declaration:
  case DW_TAG_imported_module:
  case DW_TAG_variable:
    switch (tag) {
    case DW_TAG_variable:
    case DW_TAG_constant:
    case DW_TAG_formal_parameter: {
      ConstString name;
      Type *type = nullptr;
      DWARFFormValue const_form_value;
      DWARFFormValue comptime_form_value;

      DWARFAttributes attributes = die.GetAttributes();
      for (size_t i = 0; i < attributes.Size(); ++i) {
        DWARFFormValue form_value;
        switch (attributes.AttributeAtIndex(i)) {
        default:
          break;
        case DW_AT_name:
          if (attributes.ExtractFormValueAtIndex(i, form_value))
            name.SetCString(form_value.AsCString());
          break;
        case DW_AT_type:
          if (attributes.ExtractFormValueAtIndex(i, form_value))
            type = form_value.Reference().ResolveType();
          break;
        case DW_AT_const_value:
          attributes.ExtractFormValueAtIndex(i, const_form_value);
          break;
        case DW_AT_ZIG_comptime_value:
          attributes.ExtractFormValueAtIndex(i, comptime_form_value);
          break;
        }
      }

      if (!name || !type)
        break;
      CompilerDeclContext decl_ctx =
          SymbolFileDWARF::GetContainingDeclContext(die);
      ZigType *zig_type = m_ast.UnwrapType(type->GetForwardCompilerType());
      switch (tag) {
      default:
        llvm_unreachable("already checked");
      case DW_TAG_variable:
      case DW_TAG_formal_parameter:
        zig_decl = m_ast.GetVariable(decl_ctx.GetOpaqueDeclContext(), name,
                                     zig_type->AsOpaqueType());
        break;
      case DW_TAG_constant: {
        ZigValue *zig_const_value;
        if (comptime_form_value.IsValid())
          zig_const_value = ParseComptimeValue(comptime_form_value.Reference());
        else if (const_form_value.IsValid())
          zig_const_value = ParseValue(const_form_value, zig_type);
        else
          zig_const_value = m_ast.GetOnlyPossibleValue(zig_type);
        if (!zig_const_value)
          break;
        if (ZigType *zig_const_type = llvm::dyn_cast<ZigType>(zig_const_value))
          if (llvm::isa_and_present<ZigContainer>(
                  zig_const_type->GetNamespace())) {
            zig_decl = m_ast.GetAlias(decl_ctx.GetOpaqueDeclContext(), name,
                                      zig_const_type);
            break;
          }
        zig_decl = m_ast.GetConstant(decl_ctx.GetOpaqueDeclContext(), name,
                                     zig_const_value);
        break;
      }
      }
      break;
    }
    case DW_TAG_imported_declaration:
    case DW_TAG_imported_module: {
      Type *type =
          die.GetAttributeValueAsReferenceDIE(DW_AT_import).ResolveType();
      if (!type)
        break;
      ZigType *zig_type = m_ast.UnwrapType(type->GetForwardCompilerType());
      if (!zig_type)
        break;
      CompilerDeclContext decl_ctx =
          SymbolFileDWARF::GetContainingDeclContext(die);
      if (llvm::isa_and_present<ZigContainer>(zig_type->GetNamespace()))
        zig_decl = m_ast.GetAlias(decl_ctx.GetOpaqueDeclContext(),
                                  ConstString(die.GetName()), zig_type);
      else
        zig_decl = m_ast.GetConstant(decl_ctx.GetOpaqueDeclContext(),
                                     ConstString(die.GetName()), zig_type);
      break;
    }
    default:
      llvm_unreachable("already checked");
    }
    break;
  case DW_TAG_enumeration_type:
  case DW_TAG_structure_type:
  case DW_TAG_union_type: {
    Type *type = die.ResolveType();
    if (!type)
      break;
    ZigType *zig_type = m_ast.UnwrapType(type->GetLayoutCompilerType());
    if (!zig_type ||
        !llvm::isa_and_present<ZigContainer>(zig_type->GetNamespace()))
      break;
    CompilerDeclContext parent = GetDeclContextContainingUIDFromDWARF(die);
    if (!parent)
      break;
    m_ast.SetParentScope(zig_type, parent.GetOpaqueDeclContext());
    zig_decl = m_ast.GetConstant(parent.GetOpaqueDeclContext(),
                                 ConstString(die.GetName()), zig_type);
    break;
  }
  case DW_TAG_enumerator: {
    Type *type = die.GetParent().ResolveType();
    if (!type)
      break;
    ZigTagType *zig_type = llvm::dyn_cast_if_present<ZigTagType>(
        m_ast.UnwrapType(type->GetLayoutCompilerType()));
    if (!zig_type)
      break;
    std::optional<uint64_t> value =
        die.GetAttributeValueAsOptionalUnsigned(DW_AT_const_value);
    if (!value)
      break;
    CompilerDeclContext parent = GetDeclContextContainingUIDFromDWARF(die);
    if (!parent)
      break;
    zig_decl = m_ast.GetConstant(
        parent.GetOpaqueDeclContext(), ConstString(die.GetName()),
        m_ast.GetTag(zig_type, llvm::APInt(zig_type->GetBitSize(), *value)));
    break;
  }
  case DW_TAG_variant: {
    DWARFDIE field_die = die.GetFirstChild();
    if (field_die.Tag() != DW_TAG_member)
      break;
    DWARFDIE part_die = die.GetParent();
    if (part_die.Tag() != DW_TAG_variant_part)
      break;
    DWARFDIE discr_die = part_die.GetReferencedDIE(DW_AT_discr);
    if (discr_die.Tag() != DW_TAG_member)
      break;
    Type *tag_type = discr_die.GetReferencedDIE(DW_AT_type).ResolveType();
    if (!tag_type)
      break;
    ZigTagType *zig_tag_type = llvm::dyn_cast_if_present<ZigTagType>(
        m_ast.UnwrapType(tag_type->GetLayoutCompilerType()));
    if (!zig_tag_type)
      break;
    std::optional<uint64_t> value =
        die.GetAttributeValueAsOptionalUnsigned(DW_AT_discr_value);
    if (!value)
      break;
    CompilerDeclContext parent = GetDeclContextContainingUIDFromDWARF(part_die);
    if (!parent)
      break;
    zig_decl = m_ast.GetConstant(
        parent.GetOpaqueDeclContext(), ConstString(field_die.GetName()),
        m_ast.GetTag(zig_tag_type,
                     llvm::APInt(zig_tag_type->GetBitSize(), *value)));
    break;
  }
  default:
    break;
  }
  m_die_to_decl[die.GetDIE()] = zig_decl;
  return m_ast.WrapDecl(zig_decl);
}

void DWARFASTParserZig::EnsureAllDIEsInDeclContextHaveBeenParsed(
    CompilerDeclContext decl_ctx) {
  ZigScope *zig_scope = m_ast.UnwrapScope(decl_ctx);
  for (auto it = m_decl_ctx_to_die.find(zig_scope);
       it != m_decl_ctx_to_die.end() && it->first == zig_scope;
       it = m_decl_ctx_to_die.erase(it)) {
    DWARFDIE die = it->second;
    for (DWARFDIE child_die : die.children())
      switch (child_die.Tag()) {
      default:
        GetDeclForUIDFromDWARF(child_die);
        break;
      case DW_TAG_variant_part:
        for (DWARFDIE variant_die : child_die.children())
          GetDeclForUIDFromDWARF(variant_die);
        break;
      }
    // Ensure parent to children map is populated for this module.
    for (DWARFDIE module_die : die.GetCU()->DIE().children())
      if (module_die.Tag() == DW_TAG_module)
        GetDeclContextForUIDFromDWARF(module_die);
    if (auto child_map_it = m_parent_to_children_map.find(die.GetDIE());
        child_map_it != m_parent_to_children_map.end())
      for (auto child_it = m_parent_to_children_indices[child_map_it->second],
                child_end =
                    m_parent_to_children_indices[child_map_it->second + 1];
           child_it != child_end; ++child_it)
        GetDeclForUIDFromDWARF(
            DWARFDIE(die.GetCU(), m_parent_to_children[child_it]));
  }
}

CompilerDeclContext
DWARFASTParserZig::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  if (!die)
    return m_ast.WrapScope();
  if (auto [it, inserted] =
          m_die_to_decl_ctx.try_emplace(die.GetDIE(), nullptr);
      !inserted)
    return m_ast.WrapScope(it->second);

  SymbolFileDWARF *dwarf = die.GetDWARF();
  const dw_tag_t tag = die.Tag();
  switch (tag) {
  case DW_TAG_module: {
    std::vector<
        std::pair<const DWARFDebugInfoEntry *, const DWARFDebugInfoEntry *>>
        parent_to_child;
    for (DWARFDIE child : die.children())
      if (DWARFDIE parent =
              child.GetAttributeValueAsReferenceDIE(DW_AT_ZIG_parent))
        if (parent.GetCU() == die.GetCU())
          parent_to_child.emplace_back(parent.GetDIE(), child.GetDIE());
    llvm::sort(parent_to_child);
    for (auto it = parent_to_child.begin(), end = parent_to_child.end();
         it != end;) {
      const DWARFDebugInfoEntry *parent = it->first;
      do
        m_parent_to_children.push_back(it->second);
      while (++it != end && it->first == parent);
      m_parent_to_children_indices.push_back(m_parent_to_children.size());
      m_parent_to_children_map[parent] =
          m_parent_to_children_indices.size() - 2;
    }

    ZigModule *zig_module = m_ast.GetModule(ConstString(die.GetName()));
    LinkScopeToDIE(zig_module, die);
    return m_ast.WrapScope(zig_module);
  }
  case DW_TAG_enumeration_type:
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
    if (Type *type = die.ResolveType())
      if (CompilerDeclContext decl_ctx = m_ast.GetCompilerDeclContextForType(
              type->GetForwardCompilerType()))
        return decl_ctx;
    return m_ast.WrapScope();
  case DW_TAG_inlined_subroutine:
  case DW_TAG_subprogram:
    if (die.ResolveType())
      return m_ast.WrapScope(m_die_to_decl_ctx.at(die.GetDIE()));
    return m_ast.WrapScope();
  case DW_TAG_lexical_block:
    if (CompilerDeclContext parent =
            GetDeclContextContainingUIDFromDWARF(die)) {
      ZigBlock *zig_block = m_ast.GetBlock(parent.GetOpaqueDeclContext());
      LinkScopeToDIE(zig_block, die);
      return m_ast.WrapScope(zig_block);
    }
    return m_ast.WrapScope();
  default:
    dwarf->GetObjectFile()->GetModule()->ReportError(
        "[{0:x16}]: unhandled type tag {1:x4} ({2}), please file a bug and "
        "attach the file at the start of this error message",
        die.GetOffset(), uint16_t(tag), DW_TAG_value_to_name(tag));
    return m_ast.WrapScope();
  }
}

CompilerDeclContext
DWARFASTParserZig::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  return GetDeclContextForUIDFromDWARF(die.GetParent());
}

std::string DWARFASTParserZig::GetDIEClassTemplateParams(DWARFDIE die) {
  return std::string();
}

size_t DWARFASTParserZig::ParseChildParameters(
    const DWARFDIE &parent_die, bool skip_artificial, bool &is_var_args,
    bool &has_generic_params, llvm::SmallVectorImpl<ZigType *> &param_types) {
  if (!parent_die)
    return 0;

  size_t arg_idx = 0;
  for (DWARFDIE die : parent_die.children()) {
    const dw_tag_t tag = die.Tag();
    switch (tag) {
    case DW_TAG_formal_parameter: {
      DWARFAttributes attributes = die.GetAttributes();
      if (attributes.Size() == 0) {
        arg_idx++;
        break;
      }

      DWARFFormValue param_type_die_form;
      bool is_artificial = false;
      // one of None, Auto, Register, Extern, Static, PrivateExtern

      uint32_t i;
      for (i = 0; i < attributes.Size(); ++i)
        if (DWARFFormValue form_value;
            attributes.ExtractFormValueAtIndex(i, form_value))
          switch (attributes.AttributeAtIndex(i)) {
          case DW_AT_type:
            param_type_die_form = form_value;
            break;
          case DW_AT_artificial:
            is_artificial = form_value.Boolean();
            break;
          case DW_AT_location:
          case DW_AT_const_value:
          case DW_AT_default_value:
          case DW_AT_description:
          case DW_AT_endianity:
          case DW_AT_is_optional:
          case DW_AT_segment:
          case DW_AT_variable_parameter:
          default:
          case DW_AT_abstract_origin:
          case DW_AT_sibling:
            break;
          }

      if (!skip_artificial || !is_artificial) {
        if (Type *type = param_type_die_form.Reference().ResolveType())
          param_types.push_back(
              m_ast.UnwrapType(type->GetForwardCompilerType()));
      }
      arg_idx++;
    } break;

    case DW_TAG_unspecified_parameters:
      is_var_args = true;
      break;

    case DW_TAG_template_type_parameter:
    case DW_TAG_template_value_parameter:
      has_generic_params = true;
      break;

    default:
      break;
    }
  }
  return arg_idx;
}

ZigValue *DWARFASTParserZig::ParseValue(DWARFFormValue form_value,
                                        ZigType *type) {
  if (const uint8_t *block_data = form_value.BlockData())
    return m_ast.GetData(
        type, llvm::ArrayRef<uint8_t>(block_data, form_value.Unsigned()));
  return nullptr;
}

ZigValue *DWARFASTParserZig::ParseComptimeValue(const DWARFDIE &die) {
  if (!die)
    return nullptr;
  if (auto [it, inserted] =
          m_die_to_comptime_value.try_emplace(die.GetDIE(), nullptr);
      !inserted)
    return it->second;

  ZigValue *zig_value = nullptr;
  const dw_tag_t tag = die.Tag();
  switch (tag) {
  default: {
    die.GetDWARF()->GetObjectFile()->GetModule()->ReportError(
        "[{0:x16}]: unhandled comptime value tag {1:x4} ({2}), please file a "
        "bug and "
        "attach the file at the start of this error message",
        die.GetOffset(), uint16_t(tag), DW_TAG_value_to_name(tag));
    break;
  }
  case DW_TAG_const_type:
  case DW_TAG_volatile_type:
  case DW_TAG_typedef:
  case DW_TAG_unspecified_type:
  case DW_TAG_base_type:
  case DW_TAG_pointer_type:
  case DW_TAG_array_type:
  case DW_TAG_enumeration_type:
  case DW_TAG_structure_type:
  case DW_TAG_union_type:
  case DW_TAG_subroutine_type:
    if (Type *type = die.ResolveType())
      zig_value = m_ast.UnwrapType(type->GetForwardCompilerType());
    break;
  case DW_TAG_ZIG_comptime_value: {
    Type *type = nullptr;
    DWARFFormValue const_value;
    DWARFAttributes attributes = die.GetAttributes(DWARFDIE::Recurse::no);
    for (size_t i = 0; i < attributes.Size(); ++i)
      if (DWARFFormValue form_value;
          attributes.ExtractFormValueAtIndex(i, form_value))
        switch (attributes.AttributeAtIndex(i)) {
        default:
          break;
        case DW_AT_type:
          type = form_value.Reference().ResolveType();
          break;
        case DW_AT_const_value:
          const_value = form_value;
          break;
        }
    if (!type)
      return nullptr;
    ZigType *zig_type = m_ast.UnwrapType(type->GetForwardCompilerType());
    if (!zig_type)
      return nullptr;
    switch (zig_type->GetKind()) {
    default:
      break;
    case ZigValue::Kind::ComptimeIntType:
      if (const uint8_t *block_data = const_value.BlockData()) {
        uint64_t block_size = const_value.Unsigned();
        llvm::APSInt int_value(UINT64_C(8) * block_size);
        LoadIntFromMemory(int_value, block_data, block_size);
        if (m_ast.GetByteOrder() != endian::InlHostByteOrder())
          int_value = int_value.byteSwap();
        zig_value = m_ast.GetComptimeInt(int_value);
      } else if (auto const_int = const_value.getAsSignedConstant())
        zig_value = m_ast.GetComptimeInt(
            llvm::APSInt(llvm::APInt(64, *const_int, true), false));
      break;
    case ZigValue::Kind::ComptimeFloatType:
      if (const uint8_t *block_data = const_value.BlockData())
        if (uint64_t block_size = const_value.Unsigned(); block_size == 16) {
          llvm::APInt int_value(UINT64_C(8) * block_size, 0);
          LoadIntFromMemory(int_value, block_data, block_size);
          if (m_ast.GetByteOrder() != endian::InlHostByteOrder())
            int_value = int_value.byteSwap();
          zig_value = m_ast.GetComptimeFloat(
              llvm::APFloat(llvm::APFloat::IEEEquad(), int_value));
        }
      break;
    case ZigValue::Kind::EnumLiteralType:
      if (const char *string_value = const_value.AsCString())
        zig_value = m_ast.GetEnumLiteral(ConstString(string_value));
      break;
    }
    break;
  }
  }
  m_die_to_comptime_value[die.GetDIE()] = zig_value;
  return zig_value;
}

void DWARFASTParserZig::LinkScopeToDIE(ZigScope *zig_scope,
                                       const DWARFDIE &die) {
  if (!zig_scope || !die)
    return;
  auto &entry = m_die_to_decl_ctx[die.GetDIE()];
  assert(!entry && "scope already linked to a die");
  entry = zig_scope;
  m_decl_ctx_to_die.emplace(zig_scope, die);
}

TypeSP DWARFASTParserZig::UpdateSymbolContextScopeForType(
    const SymbolContext &sc, const DWARFDIE &die, TypeSP type_sp) {
  if (!type_sp)
    return type_sp;

  DWARFDIE sc_parent_die = SymbolFileDWARF::GetParentSymbolContextDIE(die);
  dw_tag_t sc_parent_tag = sc_parent_die.Tag();

  SymbolContextScope *symbol_context_scope = nullptr;
  if (sc_parent_tag == DW_TAG_compile_unit ||
      sc_parent_tag == DW_TAG_partial_unit) {
    symbol_context_scope = sc.comp_unit;
  } else if (sc.function != nullptr && sc_parent_die) {
    symbol_context_scope =
        sc.function->GetBlock(true).FindBlockByID(sc_parent_die.GetID());
    if (symbol_context_scope == nullptr)
      symbol_context_scope = sc.function;
  } else {
    symbol_context_scope = sc.module_sp.get();
  }

  if (symbol_context_scope != nullptr)
    type_sp->SetSymbolContextScope(symbol_context_scope);
  return type_sp;
}

DWARFASTParserZig::MemberAttributes::MemberAttributes(const DWARFDIE &die) {
  DWARFAttributes attributes = die.GetAttributes(DWARFDIE::Recurse::no);
  for (size_t i = 0; i < attributes.Size(); ++i)
    if (DWARFFormValue form_value;
        attributes.ExtractFormValueAtIndex(i, form_value))
      switch (attributes.AttributeAtIndex(i)) {
      default:
        break;
      case DW_AT_artificial:
        is_artificial = form_value.Boolean();
        break;
      case DW_AT_const_expr:
        is_comptime = form_value.Boolean();
        break;
      case DW_AT_name:
        name.SetCString(form_value.AsCString());
        break;
      case DW_AT_type:
        type = form_value;
        break;
      case DW_AT_alignment:
        align = llvm::Align(form_value.Unsigned());
        break;
      case DW_AT_const_value:
      case DW_AT_default_value:
        default_value = form_value;
        break;
      case DW_AT_ZIG_comptime_value:
        comptime_value = form_value;
        break;
      case DW_AT_data_bit_offset:
        bit_offset = form_value.Unsigned();
        break;
      case DW_AT_data_member_location:
        bit_offset = form_value.Unsigned() * 8;
        break;
      }
}
