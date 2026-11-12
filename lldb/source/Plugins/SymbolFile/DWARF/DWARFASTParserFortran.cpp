//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the DWARF AST parser for the Fortran language.
///
//===----------------------------------------------------------------------===//

#include "DWARFASTParserFortran.h"
#include "DWARFDIE.h"
#include "DWARFDebugInfo.h"
#include "DWARFDeclContext.h"
#include "DWARFDefines.h"
#include "LogChannelDWARF.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDebugMap.h"
#include "UniqueDWARFASTType.h"

#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Utility/Log.h"
#include "lldb/ValueObject/ValueObject.h"

#include "llvm/Support/FormatAdapters.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;
using namespace llvm::dwarf;

DWARFASTParserFortran::DWARFASTParserFortran(
    lldb_private::TypeSystemFortran &m_ast)
    : lldb_private::plugin::dwarf::DWARFASTParser(Kind::DWARFASTParserFortran),
      m_ast(m_ast) {}

DWARFASTParserFortran::~DWARFASTParserFortran() {}

TypeSP DWARFASTParserFortran::UpdateSymbolContextScopeForType(
    const SymbolContext &sc, const DWARFDIE &die, TypeSP type_sp) {
  if (!type_sp)
    return type_sp;

  DWARFDIE sc_parent_die = SymbolFileDWARF::GetParentSymbolContextDIE(die);
  dw_tag_t sc_parent_tag = sc_parent_die.Tag();

  SymbolContextScope *symbol_context_scope = nullptr;
  if (sc_parent_tag == DW_TAG_compile_unit ||
      sc_parent_tag == DW_TAG_partial_unit) {
    symbol_context_scope = sc.comp_unit;
  } else if (!sc.function && sc_parent_die) {
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

void DWARFASTParserFortran::ParseFunctionParameters(
    const DWARFDIE &function_die,
    llvm::SmallVectorImpl<CompilerType> &param_types,
    llvm::SmallVectorImpl<llvm::StringRef> &param_names) {
  if (!function_die)
    return;

  for (DWARFDIE die : function_die.children()) {
    if (die.Tag() != DW_TAG_formal_parameter ||
        die.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
      continue;

    DWARFDIE param_type_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
    Type *type = die.ResolveTypeUID(param_type_die);
    if (!type)
      break;

    param_names.emplace_back(die.GetName());
    param_types.push_back(type->GetForwardCompilerType());
  }
  assert(param_names.size() == param_types.size());
}

lldb::TypeSP DWARFASTParserFortran::ParseTypeFromDWARF(
    const lldb_private::SymbolContext &sc,
    const lldb_private::plugin::dwarf::DWARFDIE &die, bool *type_is_new_ptr) {
  TypeSP type_sp;
  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  Log *log = GetLog(DWARFLog::TypeCompletion | DWARFLog::Lookups);

  if (!die)
    return type_sp;
  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (log) {
    dwarf->GetObjectFile()->GetModule()->LogMessage(
        log,
        "DWARFASTParserFortran::ParseTypeFromDWARF (die = 0x%8.8x) %s name"
        "= "
        "'%s')",
        die.GetOffset(), plugin::dwarf::DW_TAG_value_to_name(die.Tag()),
        die.GetName());
  }

  Type *type_ptr = dwarf->GetDIEToType().lookup(die.GetDIE());

  if (type_ptr) {
    if (type_ptr != DIE_IS_BEING_PARSED)
      return type_ptr->shared_from_this();
    else
      return TypeSP();
  }

  if (type_is_new_ptr)
    *type_is_new_ptr = true;

  ConstString type_name;
  const char *type_name_cstr = nullptr;
  CompilerType compiler_type;
  DWARFAttributes attributes;
  DWARFFormValue form_value;
  Declaration decl;
  // We use 0x00 as a sentinel value, it doesn't map to any DW_ATE attribute
  llvm::dwarf::TypeKind encoding = static_cast<llvm::dwarf::TypeKind>(0x00);

  switch (const dw_tag_t tag = die.Tag()) {
  case DW_TAG_base_type: {
    dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;
    attributes = die.GetAttributes();
    uint64_t bit_size = 0;
    for (size_t idx = 0; idx < attributes.Size(); idx++) {
      if (attributes.ExtractFormValueAtIndex(idx, form_value)) {

        switch (attributes.AttributeAtIndex(idx)) {
        case DW_AT_name:
          type_name_cstr = form_value.AsCString();

          if (type_name_cstr && type_name_cstr[0] != '\0')
            type_name.SetString(llvm::StringRef(type_name_cstr).upper());
          else
            type_name.SetCString("UNKNOWN_FORTRAN_TYPE");

          break;
        case DW_AT_encoding:
          encoding = static_cast<llvm::dwarf::TypeKind>(form_value.Unsigned());
          break;
        case DW_AT_byte_size:
          bit_size = form_value.Unsigned() * 8;
          break;
        case DW_AT_bit_size:
          bit_size = form_value.Unsigned();
          break;
        default:
          break;
        }
      }
    }

    compiler_type = m_ast.CreateBaseType(encoding, bit_size, type_name);
    type_sp =
        dwarf->MakeType(die.GetID(), type_name, llvm::divideCeil(bit_size, 8),
                        nullptr, LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                        compiler_type, Type::ResolveState::Full);
    break;
  }
  case DW_TAG_subprogram:
  case DW_TAG_subroutine_type: {
    CompilerType return_type;
    dwarf->GetDIEToType()[die.GetDIE()] = DIE_IS_BEING_PARSED;
    attributes = die.GetAttributes();
    size_t num_attr = attributes.Size();

    for (size_t i = 0; i < num_attr; ++i) {
      if (!attributes.ExtractFormValueAtIndex(i, form_value))
        continue;
      switch (attributes.AttributeAtIndex(i)) {
      case DW_AT_name: {
        const char *type_name_cstr = form_value.AsCString();
        if (type_name_cstr && type_name_cstr[0] != '\0')
          type_name.SetString(type_name_cstr);
        else
          type_name.SetCString("UNKNOWN_FORTRAN_FUNCTION");

        break;
      }
      case DW_AT_type: {
        if (DWARFDIE type_die = form_value.Reference())
          if (Type *resolved_type = die.GetDWARF()->ResolveType(type_die))
            return_type = resolved_type->GetForwardCompilerType();
        break;
      }

      default:
        break;
      }
    }

    llvm::SmallVector<CompilerType, 4> param_types;
    llvm::SmallVector<llvm::StringRef, 4> param_names;
    ParseFunctionParameters(die, param_types, param_names);

    compiler_type = m_ast.CreateFortranFunction(type_name, param_types,
                                                param_names, return_type);

    type_sp = dwarf->MakeType(die.GetID(), type_name, 0, nullptr,
                              LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                              compiler_type, Type::ResolveState::Full);
    break;
  }

  default:
    if (log)
      dwarf->GetObjectFile()->GetModule()->LogMessage(
          log, "[{0:x16}]: unhandled type tag {1:x4} ({2})", die.GetOffset(),
          tag, DW_TAG_value_to_name(tag));

    break;
  }

  UpdateSymbolContextScopeForType(sc, die, type_sp);
  if (type_sp.get())
    dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();

  return type_sp;
}

/// Walks transparent type wrappers following DW_AT_type and returns
/// the first DW_AT_byte_size encountered along the chain.
static std::optional<uint64_t> GetByteSizeFromTypeDIE(DWARFDIE die,
                                                      unsigned max_depth = 64) {
  // Bound the walk to guard against malformed/cyclic DWARF.
  if (!die || !max_depth)
    return std::nullopt;

  if (std::optional<uint64_t> byte_size =
          die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size))
    return byte_size;

  switch (die.Tag()) {
  case DW_TAG_const_type:
  case DW_TAG_volatile_type:
  case DW_TAG_restrict_type:
  case DW_TAG_atomic_type:
  case DW_TAG_typedef:
    if (DWARFDIE next = die.GetAttributeValueAsReferenceDIE(DW_AT_type))
      return GetByteSizeFromTypeDIE(next, max_depth - 1);
    break;
  default:
    break;
  }
  return std::nullopt;
}

/// Global variables that are not initialized may have their address set to
/// zero. Since multiple variables may have this address, we cannot apply the
/// OSO relink address approach we normally use.
/// However, the executable will have a matching symbol with a good address;
/// this function attempts to find the correct address by looking into the
/// executable's symbol table. If it succeeds, the expr_list is updated with
/// the new address and the executable's symbol is returned.
static Symbol *fixupExternalAddrZeroVariable(
    SymbolFileDWARFDebugMap &debug_map_symfile, llvm::StringRef name,
    DWARFExpressionList &expr_list, const DWARFDIE &die) {
  ObjectFile *debug_map_objfile = debug_map_symfile.GetObjectFile();
  if (!debug_map_objfile)
    return nullptr;

  Symtab *debug_map_symtab = debug_map_objfile->GetSymtab();
  if (!debug_map_symtab)
    return nullptr;
  Symbol *exe_symbol = debug_map_symtab->FindFirstSymbolWithNameAndType(
      ConstString(name), eSymbolTypeData, Symtab::eDebugYes,
      Symtab::eVisibilityExtern);
  if (!exe_symbol || !exe_symbol->ValueIsAddress())
    return nullptr;
  const addr_t exe_file_addr = exe_symbol->GetAddressRef().GetFileAddress();
  if (exe_file_addr == LLDB_INVALID_ADDRESS)
    return nullptr;

  DWARFExpression *location = expr_list.GetMutableExpressionAtAddress();
  if (location->Update_DW_OP_addr(die.GetCU(), exe_file_addr))
    return exe_symbol;
  return nullptr;
}

lldb::VariableSP DWARFASTParserFortran::ParseVariableFromDWARF(
    const SymbolContext &sc, const lldb_private::plugin::dwarf::DWARFDIE &die,
    const lldb::addr_t func_low_pc) {
  const dw_tag_t tag = die.Tag();

  if (tag != DW_TAG_variable && tag != DW_TAG_constant &&
      tag != DW_TAG_member && (tag != DW_TAG_formal_parameter || !sc.function))
    return nullptr;

  DWARFAttributes attributes = die.GetAttributes();
  const char *name = nullptr;
  const char *mangled = nullptr;
  Declaration decl;
  DWARFFormValue type_die_form;
  bool is_external = false;
  bool is_artificial = false;
  std::optional<uint64_t> tag_offset = std::nullopt;
  DWARFFormValue const_value_form, location_form;
  Variable::RangeList scope_ranges;

  for (size_t i = 0; i < attributes.Size(); ++i) {
    dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;

    if (!attributes.ExtractFormValueAtIndex(i, form_value))
      continue;
    switch (attr) {
    case DW_AT_LLVM_tag_offset:
      tag_offset = form_value.Unsigned();
      break;
    case DW_AT_decl_file:
      decl.SetFile(
          attributes.CompileUnitAtIndex(i)->GetFile(form_value.Unsigned()));
      break;
    case DW_AT_decl_line:
      decl.SetLine(form_value.Unsigned());
      break;
    case DW_AT_decl_column:
      decl.SetColumn(form_value.Unsigned());
      break;
    case DW_AT_name:
      name = form_value.AsCString();
      break;
    case DW_AT_linkage_name:
    case DW_AT_MIPS_linkage_name:
      mangled = form_value.AsCString();
      break;
    case DW_AT_type:
      // DW_AT_type on declaration may be less accurate than
      // that of definition, so don't overwrite it.
      if (!type_die_form.IsValid())
        type_die_form = form_value;
      break;
    case DW_AT_external:
      is_external = form_value.Boolean();
      break;
    case DW_AT_const_value:
      const_value_form = form_value;
      break;
    case DW_AT_location:
      location_form = form_value;
      break;
    case DW_AT_start_scope:
      // TODO: Implement this.
      break;
    case DW_AT_artificial:
      is_artificial = form_value.Boolean();
      break;
    case DW_AT_declaration:
    case DW_AT_description:
    case DW_AT_endianity:
    case DW_AT_segment:
    case DW_AT_specification:
    case DW_AT_visibility:
    default:
    case DW_AT_abstract_origin:
    case DW_AT_sibling:
      break;
    }
  }

  // Prefer DW_AT_location over DW_AT_const_value. Both can be emitted e.g.
  // for static constexpr member variables -- DW_AT_const_value and
  // DW_AT_location will both be present in the DIE defining the member.
  bool location_is_const_value_data =
      const_value_form.IsValid() && !location_form.IsValid();

  SymbolFileDWARF *dwarf = die.GetDWARF();
  ModuleSP module_sp = dwarf->GetObjectFile()->GetModule();

  DWARFExpressionList location_list = [&] {
    if (location_form.IsValid())
      return SymbolFileDWARF::GetExprListFromAtLocation(
          location_form, module_sp, die, func_low_pc);
    if (const_value_form.IsValid())
      return SymbolFileDWARF::GetExprListFromAtConstValue(const_value_form,
                                                          module_sp, die);
    return DWARFExpressionList(module_sp, DWARFExpression(), die.GetCU());
  }();

  const DWARFDIE parent_context_die =
      dwarf->GetDeclContextDIEContainingDIE(die);
  const DWARFDIE sc_parent_die = dwarf->GetParentSymbolContextDIE(die);
  const dw_tag_t parent_tag = sc_parent_die.Tag();
  bool is_static_member = (parent_tag == DW_TAG_compile_unit ||
                           parent_tag == DW_TAG_partial_unit) &&
                          (parent_context_die.Tag() == DW_TAG_class_type ||
                           parent_context_die.Tag() == DW_TAG_structure_type);

  ValueType scope = eValueTypeInvalid;
  SymbolContextScope *symbol_context_scope = nullptr;

  if (tag == DW_TAG_formal_parameter)
    scope = eValueTypeVariableArgument;
  else {
    // DWARF doesn't specify if a DW_TAG_variable is a local, global
    // or static variable, so we have to do a little digging:
    // 1) DW_AT_linkage_name implies static lifetime (but may be missing)
    // 2) An empty DW_AT_location is an (optimized-out) static lifetime var.
    // 3) DW_AT_location containing a DW_OP_addr implies static lifetime.
    // Clang likes to combine small global variables into the same symbol
    // with locations like: DW_OP_addr(0x1000), DW_OP_constu(2), DW_OP_plus
    // so we need to look through the whole expression.
    bool has_explicit_location = location_form.IsValid();
    bool is_static_lifetime = mangled != nullptr || (has_explicit_location &&
                                                     !location_list.IsValid());
    // Check if the location has a DW_OP_addr with any address value...
    lldb::addr_t location_DW_OP_addr = LLDB_INVALID_ADDRESS;
    if (!location_is_const_value_data) {
      if (const DWARFExpression *location =
              location_list.GetAlwaysValidExpr()) {
        if (auto maybe_location_DW_OP_addr =
                location->GetLocation_DW_OP_addr(location_form.GetUnit())) {
          location_DW_OP_addr = *maybe_location_DW_OP_addr;
        } else {
          StreamString strm;
          location->DumpLocation(&strm, eDescriptionLevelFull, nullptr);
          dwarf->GetObjectFile()->GetModule()->ReportError(
              "{0:x16}: {1} ({2}) has an invalid location: {3}: {4}",
              die.GetOffset(), DW_TAG_value_to_name(die.Tag()), die.Tag(),
              llvm::fmt_consume(maybe_location_DW_OP_addr.takeError()),
              strm.GetData());
        }
      }
      if (location_DW_OP_addr != LLDB_INVALID_ADDRESS)
        is_static_lifetime = true;
    }
    SymbolFileDWARFDebugMap *debug_map_symfile = dwarf->GetDebugMapSymfile();
    if (debug_map_symfile)
      // Set the module of the expression to the linked module
      // instead of the object file so the relocated address can be
      // found there.
      location_list.SetModule(debug_map_symfile->GetObjectFile()->GetModule());

    if (is_static_lifetime) {
      if (is_external)
        scope = eValueTypeVariableGlobal;
      else
        scope = eValueTypeVariableStatic;

      if (debug_map_symfile) {
        bool linked_oso_file_addr = false;

        if (is_external && location_DW_OP_addr == 0) {
          if (Symbol *exe_symbol = fixupExternalAddrZeroVariable(
                  *debug_map_symfile, mangled ? mangled : name, location_list,
                  die)) {
            linked_oso_file_addr = true;
            symbol_context_scope = exe_symbol;
          }
        }

        if (!linked_oso_file_addr) {
          // The DW_OP_addr is not zero, but it contains a .o file address
          // which needs to be linked up correctly.
          const lldb::addr_t exe_file_addr =
              debug_map_symfile->LinkOSOFileAddress(dwarf, location_DW_OP_addr);
          if (exe_file_addr != LLDB_INVALID_ADDRESS) {
            // Update the file address for this variable
            DWARFExpression *location =
                location_list.GetMutableExpressionAtAddress();
            location->Update_DW_OP_addr(die.GetCU(), exe_file_addr);
          } else {
            // Variable didn't make it into the final executable
            return nullptr;
          }
        }
      }
    } else {
      if (location_is_const_value_data && die.IsGlobalOrStaticScopeVariable())
        scope = eValueTypeVariableStatic;
      else {
        scope = eValueTypeVariableLocal;
        if (debug_map_symfile) {
          // We need to check for TLS addresses that we need to fixup
          if (location_list.ContainsThreadLocalStorage()) {
            location_list.LinkThreadLocalStorage(
                debug_map_symfile->GetObjectFile()->GetModule(),
                [dwarf, debug_map_symfile](
                    lldb::addr_t unlinked_file_addr) -> lldb::addr_t {
                  return debug_map_symfile->LinkOSOFileAddress(
                      dwarf, unlinked_file_addr);
                });
            scope = eValueTypeVariableThreadLocal;
          }
        }
      }
    }
  }

  if (symbol_context_scope == nullptr) {
    switch (parent_tag) {
    case DW_TAG_subprogram:
    case DW_TAG_inlined_subroutine:
    case DW_TAG_lexical_block:
      if (sc.function) {
        symbol_context_scope =
            sc.function->GetBlock(true).FindBlockByID(sc_parent_die.GetID());
        if (symbol_context_scope == nullptr)
          symbol_context_scope = sc.function;
      }
      break;

    default:
      symbol_context_scope = sc.comp_unit;
      break;
    }
  }

  if (!symbol_context_scope) {
    // Not ready to parse this variable yet. It might be a global or static
    // variable that is in a function scope and the function in the symbol
    // context wasn't filled in yet
    return nullptr;
  }

  auto type_sp = std::make_shared<SymbolFileType>(
      *dwarf, type_die_form.Reference().GetID());

  bool use_type_size_for_value =
      location_is_const_value_data &&
      DWARFFormValue::IsDataForm(const_value_form.Form());
  if (use_type_size_for_value) {
    std::optional<uint64_t> byte_size;
    if (Type *t = type_sp->GetType())
      byte_size = llvm::expectedToOptional(t->GetByteSize(nullptr));

    // Some TypeSystems (such as Swift) cannot determine a type's byte
    // size without an execution context (e.g. types whose layout
    // depends on runtime metadata). In those cases the debug info
    // might still carry the static size of the value's box, which is
    // enough if the value is a constant.
    if (!byte_size)
      byte_size = GetByteSizeFromTypeDIE(type_die_form.Reference());

    if (byte_size) {
      DWARFExpression *location = location_list.GetMutableExpressionAtAddress();
      location->UpdateValue(const_value_form.Unsigned(), *byte_size,
                            die.GetCU()->GetAddressByteSize());
    }
  }

  return std::make_shared<Variable>(
      die.GetID(), name, mangled, type_sp, scope, symbol_context_scope,
      scope_ranges, &decl, location_list, is_external, is_artificial,
      location_is_const_value_data, is_static_member, tag_offset);
}

lldb_private::Function *DWARFASTParserFortran::ParseFunctionFromDWARF(
    lldb_private::CompileUnit &comp_unit,
    const lldb_private::plugin::dwarf::DWARFDIE &die,
    lldb_private::AddressRanges ranges) {
  if (die.Tag() != DW_TAG_subprogram && die.Tag() != DW_TAG_subroutine_type)
    return nullptr;

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
  if (!die.GetDIENamesAndRanges(name, mangled, unused_func_ranges, decl_file,
                                decl_line, decl_column, call_file, call_line,
                                call_column, &frame_base))
    return nullptr;

  Mangled func_name;
  // TODO: Once the Mangled class supports Fortran demangling, we should
  // synthesize a demangled name here if GetDIENamesAndRanges fails to provide
  // one.
  if (mangled)
    func_name.SetMangledName(ConstString(mangled));

  if (name)
    func_name.SetDemangledName(ConstString(name));

  FunctionSP func_sp;
  SymbolFileDWARF *dwarf = die.GetDWARF();
  // Supply the type only if it has already been parsed
  Type *func_type = dwarf->GetDIEToType().lookup(die.GetDIE());
  assert(func_type == nullptr || func_type != DIE_IS_BEING_PARSED);
  const user_id_t func_user_id = die.GetID();
  Address func_addr = ranges[0].GetBaseAddress();

  func_sp = std::make_shared<Function>(&comp_unit,
                                       func_user_id, // UserID is the DIE offset
                                       func_user_id, func_name, func_type,
                                       std::move(func_addr), std::move(ranges));

  if (func_sp.get() == nullptr)
    return nullptr;

  if (frame_base.IsValid())
    func_sp->GetFrameBaseExpression() = frame_base;

  comp_unit.AddFunction(func_sp);
  return func_sp.get();
}

bool DWARFASTParserFortran::CompleteTypeFromDWARF(
    const lldb_private::plugin::dwarf::DWARFDIE &die, lldb_private::Type *type,
    const lldb_private::CompilerType &compiler_type) {
  return false;
}
