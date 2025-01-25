//===-- ZigStdDataFormatters.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigStdDataFormatters.h"

#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "Plugins/TypeSystem/Zig/ValueObjectZig.h"
#include "Plugins/TypeSystem/Zig/ZigValue.h"

#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Utility/DataEncoder.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/ValueObject/ValueObject.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectMemory.h"

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

#include "lldb/Core/Debugger.h"

#include <algorithm>

using namespace lldb_private;

namespace {

class ValueObjectSyntheticChild : public ValueObject {
  std::optional<uint64_t> GetByteSize() final {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    return GetCompilerType().GetByteSize(
        exe_ctx.GetBestExecutionContextScope());
  }
  std::optional<uint64_t> GetBitSize() final {
    ExecutionContext exe_ctx(GetExecutionContextRef());
    return GetCompilerType().GetBitSize(exe_ctx.GetBestExecutionContextScope());
  }

  lldb::ValueType GetValueType() const final {
    return m_parent->GetValueType();
  }

protected:
  ValueObjectSyntheticChild(ValueObject &parent) : ValueObject(parent) {}

  bool UpdateValue() final {
    m_error.Clear();
    SetValueIsValid(false);
    if (m_parent->UpdateValueIfNeeded(false))
      SetValueIsValid(true);
    else
      m_error = Status::FromErrorStringWithFormatv(
          "parent failed to evaluate: {}", m_parent->GetError());
    return m_error.Success();
  }

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override {
    return std::min(GetCompilerType().GetNumFields(), max);
  }
};

class ZigStdHashMapSyntheticFrontEnd final : public SyntheticChildrenFrontEnd {
  class EntryValueObject final : public ValueObjectSyntheticChild {
  public:
    EntryValueObject(ZigStdHashMapSyntheticFrontEnd &frontend,
                     uint32_t child_idx)
        : ValueObjectSyntheticChild(frontend.m_backend), m_frontend(frontend),
          m_slot_idx(frontend.m_slot_indices[child_idx]) {
      SetName(ConstString(llvm::formatv("[{0:d}]", child_idx).str()));
    }

  protected:
    ValueObject *CreateChildAtIndex(size_t field_idx) override {
      if (!m_frontend.m_header) {
        LLDB_LOG(GetLog(LLDBLog::Types), "could not read header field");
        return nullptr;
      }
      std::string field_name;
      if (!m_frontend.m_entry_type.GetFieldAtIndex(field_idx, field_name,
                                                   nullptr, nullptr, nullptr))
        return nullptr;
      lldb::ValueObjectSP field_array;
      if (field_name == "key")
        field_array = m_frontend.m_header->GetChildMemberWithName("keys");
      else if (field_name == "value")
        field_array = m_frontend.m_header->GetChildMemberWithName("values");
      if (!field_array)
        return nullptr;
      ValueObject *child =
          field_array->GetSyntheticArrayMember(m_slot_idx, true).get();
      if (child)
        child->SetName(TypeSystemZig::ChildFieldName(field_name));
      return child;
    }

    CompilerType GetCompilerTypeImpl() override {
      return m_frontend.m_entry_type;
    }

  private:
    ZigStdHashMapSyntheticFrontEnd &m_frontend;
    uint32_t m_slot_idx;
  };

public:
  ZigStdHashMapSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_metadata(nullptr) {}

  lldb::ChildCacheState Update() override {
    m_entry_type = CompilerType();
    m_metadata = nullptr;
    m_header = nullptr;
    m_slot_indices.clear();

    CompilerType map_type = m_backend.GetCompilerType();
    if (CompilerType deref_map_type = map_type.GetPointeeType())
      map_type = deref_map_type;
    auto type_system =
        map_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
    if (!type_system)
      return lldb::eRefetch;
    ExecutionContext exe_ctx(m_backend.GetExecutionContextRef());
    ExecutionContextScope *exe_scope = exe_ctx.GetBestExecutionContextScope();
    lldb::ValueObjectSP map_type_valobj =
        ValueObjectZig::Create(exe_scope, type_system->UnwrapType(map_type));
    if (!map_type_valobj)
      return lldb::eRefetch;
    if (ValueObjectZig *entry_type = ValueObjectZig::dyn_cast_if_present(
            map_type_valobj->GetChildMemberWithName("KV").get())) {
      auto [entry_type_system, zig_entry_value] = entry_type->UnwrapZigValue();
      if (ZigType *zig_entry_type =
              llvm::dyn_cast_if_present<ZigStructType>(zig_entry_value))
        m_entry_type = entry_type_system->WrapType(zig_entry_type);
    }
    m_metadata = m_backend.GetChildAtNamePath({"metadata", "?"}).get();
    if (!m_metadata)
      return lldb::eRefetch;
    lldb::addr_t metadata_addr = m_metadata->GetPointerValue();
    if (metadata_addr == LLDB_INVALID_ADDRESS || !metadata_addr)
      return lldb::eRefetch;
    ValueObjectZig *header_type = ValueObjectZig::dyn_cast_if_present(
        map_type_valobj->GetChildMemberWithName("Header").get());
    if (!header_type)
      return lldb::eRefetch;
    auto [header_type_system, zig_header_value] = header_type->UnwrapZigValue();
    ZigType *zig_header_type =
        llvm::dyn_cast_if_present<ZigStructType>(zig_header_value);
    if (!zig_header_type)
      return lldb::eRefetch;
    m_header = ValueObjectMemory::Create(
        exe_scope, "header", metadata_addr - zig_header_type->GetByteSize(),
        type_system->WrapType(zig_header_type));
    lldb::ValueObjectSP capacity_valobj =
        m_header->GetChildMemberWithName("capacity");
    if (!capacity_valobj)
      return lldb::eRefetch;
    for (uint32_t slot_idx = 0,
                  capacity = capacity_valobj->GetValueAsUnsigned(0);
         slot_idx != capacity; ++slot_idx) {
      lldb::ValueObjectSP slot_metadata =
          m_metadata->GetSyntheticArrayMember(slot_idx, true);
      if (!slot_metadata)
        return lldb::eRefetch;
      lldb::ValueObjectSP slot_used =
          slot_metadata->GetChildMemberWithName("used");
      if (!slot_used)
        return lldb::eRefetch;
      if (slot_used->GetValueAsUnsigned(0))
        m_slot_indices.push_back(slot_idx);
    }
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return std::min<uint64_t>(m_slot_indices.size(), UINT32_MAX);
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return name == "header" ? m_slot_indices.size() : UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (!num_children_or_err)
      return ValueObjectConstResult::Create(
          nullptr, Status::FromError(num_children_or_err.takeError()));
    if (idx >= m_slot_indices.size())
      return idx == m_slot_indices.size() ? m_header : nullptr;
    return (new EntryValueObject(*this, idx))->GetSP();
  }

private:
  CompilerType m_entry_type;
  ValueObject *m_metadata;
  lldb::ValueObjectSP m_header;
  std::vector<uint32_t> m_slot_indices;
};

class ZigStdMultiArrayListSyntheticFrontEnd final
    : public SyntheticChildrenFrontEnd {
  class ElemValueObject final : public ValueObjectSyntheticChild {
  public:
    ElemValueObject(ZigStdMultiArrayListSyntheticFrontEnd &frontend,
                    uint32_t idx)
        : ValueObjectSyntheticChild(frontend.m_backend), m_frontend(frontend),
          m_idx(idx) {
      SetName(ConstString(llvm::formatv("[{0:d}]", idx).str()));
    }

  protected:
    ValueObject *CreateChildAtIndex(size_t field_idx) override {
      ExecutionContext exe_ctx(GetExecutionContextRef());
      std::string field_name;
      CompilerType field_type = m_frontend.m_elem_type.GetFieldAtIndex(
          field_idx, field_name, nullptr, nullptr, nullptr);
      auto field_byte_size =
          field_type.GetByteSize(exe_ctx.GetBestExecutionContextScope());
      if (!field_byte_size)
        return nullptr;
      ValueObject *ptr = m_frontend.m_bytes_or_ptrs.getPointer();
      if (!ptr) {
        LLDB_LOG(GetLog(LLDBLog::Types), "could not read {0} field",
                 m_frontend.GetBytesOrPtrsFieldName());
        return nullptr;
      }
      uint32_t child_offset = *field_byte_size * m_idx;
      switch (m_frontend.GetVariant()) {
      case Variant::List:
        if (field_idx >= m_frontend.m_field_offsets.size()) {
          LLDB_LOG(GetLog(LLDBLog::Types), "could not read sizes decl");
          return nullptr;
        }
        child_offset += m_frontend.m_field_offsets[field_idx];
        break;
      case Variant::Slice:
        ptr = ptr->GetChildAtIndex(field_idx).get();
        if (!ptr)
          return nullptr;
        break;
      }
      ConstString synth_key(
          llvm::formatv("{0}.{1}", GetName(), field_name).str());
      ValueObject *child = ptr->GetSyntheticChildAtOffset(
                                  child_offset, field_type, true, synth_key)
                               .get();
      if (child)
        child->SetName(TypeSystemZig::ChildFieldName(field_name));
      return child;
    }

    CompilerType GetCompilerTypeImpl() override {
      return m_frontend.m_elem_type;
    }

  private:
    ZigStdMultiArrayListSyntheticFrontEnd &m_frontend;
    uint32_t m_idx;
  };

public:
  enum class Variant { List, Slice };

  ZigStdMultiArrayListSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp,
                                        Variant variant)
      : SyntheticChildrenFrontEnd(*valobj_sp),
        m_bytes_or_ptrs(nullptr, variant), m_len(nullptr) {}

  lldb::ChildCacheState Update() override {
    m_elem_type = CompilerType();
    m_field_offsets.clear();
    m_bytes_or_ptrs.setPointer(nullptr);
    m_len = nullptr;

    CompilerType list_type = m_backend.GetCompilerType();
    if (CompilerType deref_list_type = list_type.GetPointeeType())
      list_type = deref_list_type;
    auto type_system =
        list_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
    if (!type_system)
      return lldb::eRefetch;
    switch (GetVariant()) {
    case Variant::List:
      break;
    case Variant::Slice: {
      ZigNamespace *child = type_system->UnwrapType(list_type)->GetNamespace();
      if (!child)
        return lldb::eRefetch;
      ZigContainer *parent =
          llvm::dyn_cast_if_present<ZigContainer>(child->GetParent());
      if (!parent)
        return lldb::eRefetch;
      list_type = type_system->WrapType(parent->GetOwner());
      break;
    }
    }
    lldb::ValueObjectSP list_type_valobj = ValueObjectZig::Create(
        ExecutionContext(m_backend.GetExecutionContextRef())
            .GetBestExecutionContextScope(),
        type_system->UnwrapType(list_type));
    if (!list_type_valobj)
      return lldb::eRefetch;
    if (ValueObjectZig *elem_type = ValueObjectZig::dyn_cast_if_present(
            list_type_valobj->GetChildMemberWithName("Elem").get())) {
      auto [elem_type_system, zig_elem_value] = elem_type->UnwrapZigValue();
      if (ZigType *zig_elem_type =
              llvm::dyn_cast_if_present<ZigType>(zig_elem_value))
        m_elem_type = elem_type_system->WrapType(zig_elem_type);
    }
    uint32_t num_fields = m_elem_type.GetNumFields();
    lldb::ValueObjectSP sizes =
        list_type_valobj->GetChildMemberWithName("sizes");
    if (!sizes)
      return lldb::eRefetch;
    lldb::ValueObjectSP bytes_array;
    switch (GetVariant()) {
    case Variant::List: {
      bytes_array = sizes->GetChildMemberWithName("bytes");
      if (!bytes_array)
        return lldb::eRefetch;
      llvm::Expected<uint32_t> bytes_len_or_err = bytes_array->GetNumChildren();
      if (!bytes_len_or_err || *bytes_len_or_err != num_fields)
        return lldb::eRefetch;
      break;
    }
    case Variant::Slice:
      break;
    }
    lldb::ValueObjectSP fields_array = sizes->GetChildMemberWithName("fields");
    if (!fields_array)
      return lldb::eRefetch;
    llvm::Expected<uint32_t> fields_len_or_err = fields_array->GetNumChildren();
    if (!fields_len_or_err || *fields_len_or_err != num_fields)
      return lldb::eRefetch;
    switch (GetVariant()) {
    case Variant::List: {
      m_field_offsets.resize(num_fields);
      uint32_t capacity = 0;
      if (lldb::ValueObjectSP capacity_valobj =
              m_backend.GetChildMemberWithName("capacity"))
        capacity = capacity_valobj->GetValueAsUnsigned(0);
      uint32_t field_offset = 0;
      for (uint32_t idx = 0; idx != num_fields; ++idx) {
        lldb::ValueObjectSP field_idx_valobj =
            fields_array->GetChildAtIndex(idx);
        if (!field_idx_valobj)
          return lldb::eRefetch;
        uint64_t field_idx = field_idx_valobj->GetValueAsUnsigned(num_fields);
        if (field_idx >= num_fields)
          return lldb::eRefetch;
        m_field_offsets[field_idx] = field_offset;

        if (idx != num_fields - 1) {
          lldb::ValueObjectSP bytes = bytes_array->GetChildAtIndex(idx);
          if (!bytes)
            return lldb::eRefetch;
          field_offset += bytes->GetValueAsUnsigned(0) * capacity;
        }
      }
      break;
    }
    case Variant::Slice:
      break;
    }
    m_bytes_or_ptrs.setPointer(
        m_backend.GetChildMemberWithName(GetBytesOrPtrsFieldName()).get());
    m_len = m_backend.GetChildMemberWithName("len").get();
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    if (m_len)
      return std::min<uint64_t>(m_len->GetValueAsUnsigned(0), UINT32_MAX);
    return llvm::createStringError("could not read len field");
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError()) {
      consumeError(std::move(err));
      return UINT32_MAX;
    }
    llvm::StringRef name_ref = name.GetStringRef();
    uint32_t idx;
    if (name_ref.consume_front("[") && name_ref.consume_back("]") &&
        !name_ref.getAsInteger(10, idx) && idx < *num_children_or_err)
      return idx;
    idx = m_elem_type.GetIndexOfFieldWithName(name.GetCString());
    if (idx != UINT32_MAX)
      return *num_children_or_err + idx;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    if (idx < *num_children_or_err)
      return (new ElemValueObject(*this, idx))->GetSP();
    idx -= *num_children_or_err;
    std::string field_name;
    CompilerType field_array_type =
        m_elem_type.GetFieldAtIndex(idx, field_name, nullptr, nullptr, nullptr)
            .GetArrayType(*num_children_or_err);
    if (!field_array_type)
      return nullptr;
    ValueObject *ptr = m_bytes_or_ptrs.getPointer();
    if (!ptr)
      return ValueObjectConstResult::Create(
          nullptr,
          Status::FromError(llvm::createStringError(llvm::formatv(
              "could not read {0} field", GetBytesOrPtrsFieldName()))));
    uint32_t child_offset = 0;
    switch (GetVariant()) {
    case Variant::List:
      if (idx >= m_field_offsets.size())
        return ValueObjectConstResult::Create(
            nullptr, Status::FromError(
                         llvm::createStringError("could not read sizes decl")));
      child_offset = m_field_offsets[idx];
      break;
    case Variant::Slice:
      ptr = ptr->GetChildAtIndex(idx).get();
      if (!ptr)
        return nullptr;
      break;
    }
    return ptr->GetSyntheticChildAtOffset(child_offset, field_array_type, true,
                                          ConstString(field_name));
  }

private:
  Variant GetVariant() { return m_bytes_or_ptrs.getInt(); }

  llvm::StringRef GetBytesOrPtrsFieldName() {
    switch (GetVariant()) {
    case Variant::List:
      return "bytes";
    case Variant::Slice:
      return "ptrs";
    }
  }

  CompilerType m_elem_type;
  llvm::SmallVector<uint32_t, 4> m_field_offsets;
  llvm::PointerIntPair<ValueObject *, 1, Variant> m_bytes_or_ptrs;
  ValueObject *m_len;
};

class ZigStdSegmentedListSyntheticFrontEnd final
    : public SyntheticChildrenFrontEnd {
public:
  ZigStdSegmentedListSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_prealloc_segment(nullptr),
        m_prealloc_item_count(0), m_prealloc_exp(0),
        m_dynamic_segments(nullptr), m_len(nullptr) {}

  lldb::ChildCacheState Update() override {
    m_prealloc_segment =
        m_backend.GetChildMemberWithName("prealloc_segment").get();
    m_prealloc_item_count = 0;
    if (m_prealloc_segment)
      if (auto prealloc_item_count_or_err =
              m_prealloc_segment->GetNumChildren())
        m_prealloc_item_count = *prealloc_item_count_or_err;
    m_prealloc_exp = 0;
    if (m_prealloc_item_count)
      m_prealloc_exp = llvm::Log2_32(m_prealloc_item_count);
    m_dynamic_segments =
        m_backend.GetChildMemberWithName("dynamic_segments").get();
    if (m_dynamic_segments)
      m_dynamic_segments = m_dynamic_segments->GetSyntheticValue().get();
    m_len = m_backend.GetChildMemberWithName("len").get();
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    if (m_len)
      return std::min<uint64_t>(m_len->GetValueAsUnsigned(0), UINT32_MAX);
    return llvm::createStringError("could not read len field");
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError()) {
      consumeError(std::move(err));
      return UINT32_MAX;
    }
    llvm::StringRef name_ref = name.GetStringRef();
    uint64_t idx;
    if (m_len && name_ref.consume_front("[") && name_ref.consume_back("]") &&
        !name_ref.getAsInteger(10, idx) && idx < *num_children_or_err)
      return idx;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    if (idx >= *num_children_or_err)
      return nullptr;
    if (idx < m_prealloc_item_count)
      return m_prealloc_segment->GetChildAtIndex(idx);
    if (!m_dynamic_segments)
      return ValueObjectConstResult::Create(
          nullptr, Status::FromError(llvm::createStringError(
                       "could not read dynamic_segments field")));
    uint32_t shelf_idx, box_idx;
    if (m_prealloc_item_count == 0) {
      shelf_idx = llvm::Log2_32(idx + 1);
      box_idx = (idx + 1) - (UINT32_C(1) << shelf_idx);
    } else {
      shelf_idx =
          llvm::Log2_32(idx + m_prealloc_item_count) - m_prealloc_exp - 1;
      box_idx = idx + m_prealloc_item_count -
                (UINT32_C(1) << ((m_prealloc_exp + 1) + shelf_idx));
    }
    lldb::ValueObjectSP dynamic_segment =
        m_dynamic_segments->GetChildAtIndex(shelf_idx);
    if (!dynamic_segment)
      return ValueObjectConstResult::Create(
          nullptr, Status::FromError(llvm::createStringError(
                       "could not read dynamic_segments element")));
    lldb::ValueObjectSP child =
        dynamic_segment->GetSyntheticArrayMember(box_idx, true);
    if (child)
      child->SetName(ConstString(llvm::formatv("[{0:d}]", idx).str()));
    return child;
  }

private:
  ValueObject *m_prealloc_segment;
  uint32_t m_prealloc_item_count;
  uint32_t m_prealloc_exp;
  ValueObject *m_dynamic_segments;
  ValueObject *m_len;
};

} // namespace

SyntheticChildrenFrontEnd *formatters::ZigStdHashMapSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigStdHashMapSyntheticFrontEnd(valobj_sp) : nullptr;
}

SyntheticChildrenFrontEnd *
formatters::ZigStdMultiArrayListSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigStdMultiArrayListSyntheticFrontEnd(
                         valobj_sp,
                         ZigStdMultiArrayListSyntheticFrontEnd::Variant::List)
                   : nullptr;
}

SyntheticChildrenFrontEnd *
formatters::ZigStdMultiArrayListSliceSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigStdMultiArrayListSyntheticFrontEnd(
                         valobj_sp,
                         ZigStdMultiArrayListSyntheticFrontEnd::Variant::Slice)
                   : nullptr;
}

SyntheticChildrenFrontEnd *
formatters::ZigStdSegmentedListSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigStdSegmentedListSyntheticFrontEnd(valobj_sp)
                   : nullptr;
}
