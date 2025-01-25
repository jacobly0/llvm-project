//===-- DWARFASTParserZig.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERZIG_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERZIG_H

#include "DWARFASTParser.h"

#include "DWARFFormValue.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Alignment.h"

#include <unordered_map>
#include <vector>

namespace lldb_private {
class TypeSystemZig;
class ZigDeclaration;
class ZigScope;
class ZigType;
class ZigValue;
namespace plugin::dwarf {
class DWARFDIE;
class DWARFDebugInfoEntry;
} // namespace plugin::dwarf
} // namespace lldb_private

struct ParsedDWARFTypeAttributesZig;

class DWARFASTParserZig : public lldb_private::plugin::dwarf::DWARFASTParser {
public:
  DWARFASTParserZig(lldb_private::TypeSystemZig &ast);

  ~DWARFASTParserZig() override;

  // DWARFASTParser interface.
  lldb::TypeSP
  ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                     const lldb_private::plugin::dwarf::DWARFDIE &die,
                     bool *type_is_new_ptr = nullptr) override;

  lldb_private::ConstString ConstructDemangledNameFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const lldb_private::plugin::dwarf::DWARFDIE &die,
                         lldb_private::AddressRanges ranges) override;

  bool CompleteTypeFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die,
      lldb_private::Type *type,
      const lldb_private::CompilerType &compiler_type) override;

  lldb_private::CompilerDecl GetDeclForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  void EnsureAllDIEsInDeclContextHaveBeenParsed(
      lldb_private::CompilerDeclContext decl_context) override;

  lldb_private::CompilerDeclContext GetDeclContextForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  lldb_private::CompilerDeclContext GetDeclContextContainingUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  /// Returns the template parameters of a class DWARFDIE as a string.
  ///
  /// This is mostly useful for -gsimple-template-names which omits template
  /// parameters from the DIE name and instead always adds template parameter
  /// children DIEs.
  ///
  /// \param die The struct/class DWARFDIE containing template parameters.
  /// \return A string, including surrounding '<>', of the template parameters.
  /// If the DIE's name already has '<>', returns an empty string because
  /// it's assumed that the caller is using the DIE name anyway.
  std::string
  GetDIEClassTemplateParams(lldb_private::plugin::dwarf::DWARFDIE die) override;

protected:
  /// Protected typedefs and members.
  /// @{
  lldb_private::TypeSystemZig &m_ast;
  std::vector<const lldb_private::plugin::dwarf::DWARFDebugInfoEntry *>
      m_parent_to_children;
  std::vector<uint32_t> m_parent_to_children_indices;
  llvm::DenseMap<const lldb_private::plugin::dwarf::DWARFDebugInfoEntry *,
                 uint32_t>
      m_parent_to_children_map;
  llvm::DenseMap<const lldb_private::plugin::dwarf::DWARFDebugInfoEntry *,
                 lldb_private::ZigScope *>
      m_die_to_decl_ctx;
  std::unordered_multimap<lldb_private::ZigScope *,
                          lldb_private::plugin::dwarf::DWARFDIE>
      m_decl_ctx_to_die;
  llvm::DenseMap<const lldb_private::plugin::dwarf::DWARFDebugInfoEntry *,
                 lldb_private::ZigDeclaration *>
      m_die_to_decl;
  llvm::DenseMap<const lldb_private::plugin::dwarf::DWARFDebugInfoEntry *,
                 lldb_private::ZigValue *>
      m_die_to_comptime_value;
  /// @}

  size_t ParseChildParameters(
      const lldb_private::plugin::dwarf::DWARFDIE &parent_die,
      bool skip_artificial, bool &is_var_args, bool &has_generic_params,
      llvm::SmallVectorImpl<lldb_private::ZigType *> &param_types);

  lldb_private::ZigValue *
  ParseValue(lldb_private::plugin::dwarf::DWARFFormValue form_value,
             lldb_private::ZigType *type);

  lldb_private::ZigValue *
  ParseComptimeValue(const lldb_private::plugin::dwarf::DWARFDIE &die);

  void LinkScopeToDIE(lldb_private::ZigScope *zig_scope,
                      const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// If \p type_sp is valid, calculate and set its symbol context scope,
  /// and update the type list for its backing symbol file.
  ///
  /// Returns \p type_sp.
  lldb::TypeSP UpdateSymbolContextScopeForType(
      const lldb_private::SymbolContext &sc,
      const lldb_private::plugin::dwarf::DWARFDIE &die, lldb::TypeSP type_sp);

  static bool classof(const DWARFASTParser *Parser) {
    return Parser->GetKind() == Kind::DWARFASTParserZig;
  }

private:
  /// Parsed form of all attributes that are relevant for parsing type members.
  struct MemberAttributes {
    explicit MemberAttributes(const lldb_private::plugin::dwarf::DWARFDIE &die);
    bool is_artificial = false;
    bool is_comptime = false;
    lldb_private::ConstString name;
    lldb_private::plugin::dwarf::DWARFFormValue type;
    llvm::MaybeAlign align;
    lldb_private::plugin::dwarf::DWARFFormValue default_value;
    lldb_private::plugin::dwarf::DWARFFormValue comptime_value;
    uint64_t bit_offset = 0;
  };
};

/// Parsed form of all attributes that are relevant for type reconstruction.
/// Some attributes are relevant for all kinds of types (declaration), while
/// others are only meaningful to a specific type (is_virtual)
struct ParsedDWARFTypeAttributesZig {
  explicit ParsedDWARFTypeAttributesZig(
      const lldb_private::plugin::dwarf::DWARFDIE &die);

  lldb_private::ConstString name;
  lldb_private::Declaration decl;
  lldb_private::plugin::dwarf::DWARFFormValue abstract_origin;
  lldb_private::plugin::dwarf::DWARFFormValue signature;
  lldb_private::plugin::dwarf::DWARFFormValue type;
  lldb_private::plugin::dwarf::DWARFFormValue sentinel;
  std::optional<uint64_t> bit_size;
  std::optional<uint64_t> byte_size;
  llvm::MaybeAlign align;
  uint8_t encoding = 0;
  bool is_vector = false;
  uint8_t address_class = 0;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERZIG_H
