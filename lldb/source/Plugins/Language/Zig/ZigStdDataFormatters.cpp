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
  llvm::Expected<uint64_t> GetByteSize() final {
    return GetCompilerType().GetByteSize(
        ExecutionContext(GetExecutionContextRef())
            .GetBestExecutionContextScope());
  }
  llvm::Expected<uint64_t> GetBitSize() final {
    return GetCompilerType().GetBitSize(
        ExecutionContext(GetExecutionContextRef())
            .GetBestExecutionContextScope());
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
      field_array =
          m_frontend.m_header->GetChildMemberWithName(field_name + 's');
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
    ExecutionContextScope *exe_scope =
        ExecutionContext(m_backend.GetExecutionContextRef())
            .GetBestExecutionContextScope();
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
    Address metadata_addr(m_metadata->GetPointerValue().address);
    if (!metadata_addr.GetOffset())
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
    if (!metadata_addr.Slide(-zig_header_type->GetByteSize()))
      return lldb::eRefetch;
    m_header =
        ValueObjectMemory::Create(exe_scope, "header", metadata_addr,
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

  uint64_t GetNumChildren() { return m_slot_indices.size(); }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return std::min<uint64_t>(GetNumChildren(), UINT32_MAX);
  }

  llvm::Expected<size_t> GetIndexOfChildWithName(ConstString name) override {
    uint64_t base = GetNumChildren();
    if (name == "header")
      return base;
    ++base;
    return llvm::createStringErrorV("Type has no child named '{0}'", name);
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    uint64_t base = GetNumChildren();
    if (idx < base)
      return (new EntryValueObject(*this, idx))->GetSP();
    idx -= base;
    if (!idx--)
      return m_header;
    return nullptr;
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
      std::string field_name;
      CompilerType field_type = m_frontend.m_elem_type.GetFieldAtIndex(
          field_idx, field_name, nullptr, nullptr, nullptr);
      auto field_byte_size =
          field_type.GetByteSize(ExecutionContext(GetExecutionContextRef())
                                     .GetBestExecutionContextScope());
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
    m_capacity = m_backend.GetChildMemberWithName("capacity").get();
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint64_t> GetNumChildren() {
    if (m_len) {
      bool success;
      uint64_t num_children = m_len->GetValueAsUnsigned(0, &success);
      if (success)
        return num_children;
    }
    return llvm::createStringError("could not read len field");
  }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    auto num_children_or_err = GetNumChildren();
    if (auto err = num_children_or_err.takeError())
      return err;
    return std::min<uint64_t>(*num_children_or_err, UINT32_MAX);
  }

  llvm::Expected<size_t> GetIndexOfChildWithName(ConstString name) override {
    auto num_children_or_err = GetNumChildren();
    if (auto err = num_children_or_err.takeError())
      return err;
    uint64_t base = *num_children_or_err;
    llvm::StringRef name_ref = name.GetStringRef();
    size_t idx;
    if (name_ref.consume_front("[") && name_ref.consume_back("]") &&
        !name_ref.getAsInteger(10, idx) && idx < base)
      return idx;
    if (name == GetBytesOrPtrsFieldName())
      return base;
    ++base;
    if (name == "len")
      return base;
    ++base;
    if (name == "capacity")
      return base;
    ++base;
    auto idx_or_err =
        m_elem_type.GetIndexOfChildWithName(name.GetCString(), false);
    if (auto err = idx_or_err.takeError())
      return err;
    return base + *idx_or_err;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = GetNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    uint64_t num_children = *num_children_or_err;
    if (idx < num_children)
      return (new ElemValueObject(*this, idx))->GetSP();
    idx -= num_children;
    if (!idx--)
      return m_bytes_or_ptrs.getPointer()->GetSP();
    if (!idx--)
      return m_len->GetSP();
    if (!idx--)
      return m_capacity->GetSP();
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
  ValueObject *m_len, *m_capacity;
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
