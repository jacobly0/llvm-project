//===-- ZigLanguage.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigLanguage.h"

#include "ZigDataFormatters.h"
#include "ZigStdDataFormatters.h"

#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "Plugins/TypeSystem/Zig/ValueObjectZig.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/DataFormatters/FormattersHelpers.h"

using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ZigLanguage)

static void LoadStdFormatters(lldb::TypeCategoryImplSP zig_category_sp) {
  if (!zig_category_sp)
    return;

  formatters::AddStringSummary(zig_category_sp,
                               "len=${svar%#} capacity=${svar.header.capacity}",
                               "^std\\.hash_map\\.HashMapUnmanaged\\(.+\\)$",
                               TypeSummaryImpl::Flags(), true);
  formatters::AddCXXSynthetic(zig_category_sp,
                              formatters::ZigStdHashMapSyntheticFrontEndCreator,
                              "zig std.HashMap synthetic children",
                              "^std\\.hash_map\\.HashMapUnmanaged\\(.+\\)$",
                              SyntheticChildren::Flags(), true);

  formatters::AddStringSummary(
      zig_category_sp, "len=${var.len} capacity=${var.capacity}",
      "^std\\.multi_array_list\\.MultiArrayList\\(.+\\)$",
      TypeSummaryImpl::Flags(), true);
  formatters::AddCXXSynthetic(
      zig_category_sp, formatters::ZigStdMultiArrayListSyntheticFrontEndCreator,
      "zig std.MultiArrayList synthetic children",
      "^std\\.multi_array_list\\.MultiArrayList\\(.+\\)$",
      SyntheticChildren::Flags(), true);
  formatters::AddStringSummary(
      zig_category_sp, "len=${var.len} capacity=${var.capacity}",
      "^std\\.multi_array_list\\.MultiArrayList\\(.+\\)\\.Slice$",
      TypeSummaryImpl::Flags(), true);
  formatters::AddCXXSynthetic(
      zig_category_sp,
      formatters::ZigStdMultiArrayListSliceSyntheticFrontEndCreator,
      "zig std.MultiArrayList.Slice synthetic children",
      "^std\\.multi_array_list\\.MultiArrayList\\(.+\\)\\.Slice$",
      SyntheticChildren::Flags(), true);

  formatters::AddStringSummary(zig_category_sp, "len=${svar%#}",
                               "^std\\.segmented_list\\.SegmentedList\\(.+\\)$",
                               TypeSummaryImpl::Flags(), true);
  formatters::AddCXXSynthetic(
      zig_category_sp, formatters::ZigStdSegmentedListSyntheticFrontEndCreator,
      "zig std.SegmentedList synthetic children",
      "^std\\.segmented_list\\.SegmentedList\\(.+\\)$",
      SyntheticChildren::Flags(), true);
}

std::unique_ptr<Language::TypeScavenger> ZigLanguage::GetTypeScavenger() {
  class ZigTypeScavenger : public Language::ImageListTypeScavenger {
  public:
    CompilerType AdjustForInclusion(CompilerType &candidate) override {
      lldb::LanguageType lang_type(candidate.GetMinimumLanguage());
      return lang_type == GetLanguageTypeStatic() ? candidate : CompilerType();
    }
  };
  return std::make_unique<ZigTypeScavenger>();
}

lldb::TypeCategoryImplSP ZigLanguage::GetFormatters() {
  static llvm::once_flag g_initialize;
  static lldb::TypeCategoryImplSP g_category;
  llvm::call_once(g_initialize, []() {
    DataVisualization::Categories::GetCategory(
        ConstString(GetPluginNameStatic()), g_category);
    if (g_category) {
      g_category->AddLanguage(GetLanguageTypeStatic());
      LoadStdFormatters(g_category);
    }
  });
  return g_category;
}

HardcodedFormatters::HardcodedSummaryFinder
ZigLanguage::GetHardcodedSummaries() {
  static llvm::once_flag g_initialize;
  static HardcodedFormatters::HardcodedSummaryFinder g_formatters;
  llvm::call_once(g_initialize, []() {
    g_formatters.push_back([](ValueObject &valobj, lldb::DynamicValueType,
                              FormatManager &)
                               -> CXXFunctionSummaryFormat::SharedPointer {
      static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
          new CXXFunctionSummaryFormat(
              TypeSummaryImpl::Flags().SetDontShowChildren(),
              formatters::ZigAliasSummaryProvider,
              "zig alias summary provider"));
      if (ValueObjectZig *zig_valobj = ValueObjectZig::dyn_cast(&valobj))
        if (llvm::isa_and_present<ZigAlias>(zig_valobj->GetZigValue().second))
          return formatter_sp;
      return nullptr;
    });
    g_formatters.push_back([](ValueObject &valobj, lldb::DynamicValueType,
                              FormatManager &)
                               -> CXXFunctionSummaryFormat::SharedPointer {
      static CXXFunctionSummaryFormat::SharedPointer formatter_sp(
          new CXXFunctionSummaryFormat(TypeSummaryImpl::Flags(),
                                       formatters::ZigComptimeSummaryProvider,
                                       "zig comptime summary provider"));
      if (CompilerType type = valobj.GetCompilerType())
        if (auto type_system =
                type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
          if (type_system->UnwrapType(type)->HasComptimeState())
            return formatter_sp;
      return nullptr;
    });
    g_formatters.push_back([](ValueObject &valobj, lldb::DynamicValueType,
                              FormatManager &)
                               -> StringSummaryFormat::SharedPointer {
      static StringSummaryFormat::SharedPointer formatter_sp(
          new StringSummaryFormat(TypeSummaryImpl::Flags(), "len=${var.len}"));
      CompilerType type = valobj.GetCompilerType();
      if (auto type_system =
              type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
        if (ZigPointerType *zig_type =
                llvm::dyn_cast<ZigPointerType>(type_system->UnwrapType(type)))
          if (zig_type->GetSize() == ZigPointerType::Size::Slice)
            return formatter_sp;
      return nullptr;
    });
  });
  return g_formatters;
}

HardcodedFormatters::HardcodedSyntheticFinder
ZigLanguage::GetHardcodedSynthetics() {
  static llvm::once_flag g_initialize;
  static HardcodedFormatters::HardcodedSyntheticFinder g_formatters;
  llvm::call_once(g_initialize, []() {
    g_formatters.push_back(
        [](ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> SyntheticChildren::SharedPointer {
          static CXXSyntheticChildren::SharedPointer formatter_sp(
              new CXXSyntheticChildren(
                  SyntheticChildren::Flags(),
                  "zig array pointer synthetic children",
                  formatters::ZigArrayPointerSyntheticFrontEndCreator));
          CompilerType type = valobj.GetCompilerType();
          if (auto type_system =
                  type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
            if (ZigPointerType *zig_type = llvm::dyn_cast<ZigPointerType>(
                    type_system->UnwrapType(type)))
              if (zig_type->GetSize() == ZigPointerType::Size::One &&
                  llvm::isa<ZigArrayType>(zig_type->GetChildType()))
                return formatter_sp;
          return nullptr;
        });
    g_formatters.push_back(
        [](ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> SyntheticChildren::SharedPointer {
          static CXXSyntheticChildren::SharedPointer formatter_sp(
              new CXXSyntheticChildren(
                  SyntheticChildren::Flags(), "zig slice synthetic children",
                  formatters::ZigSliceSyntheticFrontEndCreator));
          CompilerType type = valobj.GetCompilerType();
          if (auto type_system =
                  type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
            if (ZigPointerType *zig_type = llvm::dyn_cast<ZigPointerType>(
                    type_system->UnwrapType(type)))
              if (zig_type->GetSize() == ZigPointerType::Size::Slice)
                return formatter_sp;
          return nullptr;
        });
    g_formatters.push_back(
        [](ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> SyntheticChildren::SharedPointer {
          static CXXSyntheticChildren::SharedPointer formatter_sp(
              new CXXSyntheticChildren(
                  SyntheticChildren::Flags(),
                  "zig error union synthetic children",
                  formatters::ZigErrorUnionSyntheticFrontEndCreator));
          CompilerType type = valobj.GetCompilerType();
          if (auto type_system =
                  type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
            if (llvm::isa<ZigErrorUnionType>(type_system->UnwrapType(type)))
              return formatter_sp;
          return nullptr;
        });
    g_formatters.push_back(
        [](ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> SyntheticChildren::SharedPointer {
          static CXXSyntheticChildren::SharedPointer formatter_sp(
              new CXXSyntheticChildren(
                  SyntheticChildren::Flags(), "zig optional synthetic children",
                  formatters::ZigOptionalSyntheticFrontEndCreator));
          CompilerType type = valobj.GetCompilerType();
          if (auto type_system =
                  type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
            if (llvm::isa<ZigOptionalType>(type_system->UnwrapType(type)))
              return formatter_sp;
          return nullptr;
        });
    g_formatters.push_back(
        [](ValueObject &valobj, lldb::DynamicValueType,
           FormatManager &) -> SyntheticChildren::SharedPointer {
          static CXXSyntheticChildren::SharedPointer formatter_sp(
              new CXXSyntheticChildren(
                  SyntheticChildren::Flags(),
                  "zig tagged union synthetic children",
                  formatters::ZigTaggedUnionSyntheticFrontEndCreator));
          CompilerType type = valobj.GetCompilerType();
          if (auto type_system =
                  type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
            if (llvm::isa<ZigTaggedUnionType>(type_system->UnwrapType(type)))
              return formatter_sp;
          return nullptr;
        });
  });
  return g_formatters;
}

void ZigLanguage::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(), "Zig Language",
                                CreateInstance);
}

LazyBool ZigLanguage::IsLogicalTrue(ValueObject &valobj, Status &error) {
  CompilerType type = valobj.GetCompilerType();
  auto type_system = type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
  if (!type_system) {
    error = Status::FromErrorString("unexpected type system type");
    return eLazyBoolNo;
  }
  if (!llvm::isa_and_present<ZigBoolType>(type_system->UnwrapType(type))) {
    error = Status::FromErrorStringWithFormatv(
        "expected type 'bool', found '{0}'", type.GetDisplayTypeName());
    return eLazyBoolNo;
  }
  bool success;
  uint64_t val = valobj.GetValueAsUnsigned(2, &success);
  if (!success) {
    error = Status::FromErrorString("failed to get a unsigned result");
    return eLazyBoolNo;
  }
  switch (val) {
  case 0:
    error.Clear();
    return eLazyBoolNo;
  case 1:
    error.Clear();
    return eLazyBoolYes;
  default:
    error = Status::FromErrorString("corrupt value");
    return eLazyBoolNo;
  }
}

bool ZigLanguage::IsNilReference(ValueObject &valobj) {
  if (valobj.GetObjectRuntimeLanguage() != GetLanguageType())
    return false;
  CompilerType type = valobj.GetCompilerType();
  auto type_system = type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
  if (!type_system)
    return false;
  ZigType *zig_type = type_system->UnwrapType(type);
  if (llvm::isa_and_present<ZigNullType>(zig_type)) {
    return true;
  } else if (llvm::isa_and_present<ZigOptionalType>(zig_type)) {
    if (auto num_children_or_err = valobj.GetNumChildren(1))
      return *num_children_or_err == 0;
  } else if (ZigPointerType *ptr_type =
                 llvm::dyn_cast_if_present<ZigPointerType>(zig_type)) {
    if (ptr_type->GetSize() == ZigPointerType::Size::C) {
      bool can_read_val = false;
      uint64_t val = valobj.GetValueAsUnsigned(0, &can_read_val);
      return can_read_val && val == 0;
    }
  }
  return false;
}

void ZigLanguage::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

// Static Functions

Language *ZigLanguage::CreateInstance(lldb::LanguageType language) {
  return language == GetLanguageTypeStatic() ? new ZigLanguage : nullptr;
}
