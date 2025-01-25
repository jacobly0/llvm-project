//===-- ZigDataFormatters.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigDataFormatters.h"

#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "Plugins/TypeSystem/Zig/ValueObjectZig.h"
#include "Plugins/TypeSystem/Zig/ZigValue.h"

#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/ValueObject/ValueObject.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

#include "llvm/Support/FormatVariadic.h"

#include <algorithm>

using namespace lldb_private;

namespace {

class ZigArrayPointerSyntheticFrontEnd final
    : public SyntheticChildrenFrontEnd {
public:
  ZigArrayPointerSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp) {}

  lldb::ChildCacheState Update() override {
    m_type = nullptr;
    CompilerType type = m_backend.GetCompilerType();
    if (auto type_system =
            type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>())
      if (ZigPointerType *pointer_type =
              llvm::dyn_cast<ZigPointerType>(type_system->UnwrapType(type)))
        if (pointer_type->GetSize() == ZigPointerType::Size::One &&
            llvm::isa<ZigArrayType>(pointer_type->GetChildType()))
          m_type = pointer_type;
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return false; }

  llvm::Expected<uint32_t> CalculateNumChildren() override { return 0; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    if (name == "ptr")
      return 0;
    if (name == "len")
      return 1;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    if (!m_type)
      return ValueObjectConstResult::Create(
          nullptr,
          Status::FromError(llvm::createStringError("could not get type")));
    TypeSystemZig *type_system = m_type->GetTypeSystem();
    ZigArrayType *array_type = llvm::cast<ZigArrayType>(m_type->GetChildType());
    ExecutionContext exe_ctx(m_backend.GetExecutionContextRef());
    ExecutionContextScope *exe_scope = exe_ctx.GetBestExecutionContextScope();
    switch (idx) {
    case 0: {
      CompilerType ptr_type = type_system->GetPointerType(
          ZigPointerType::Size::Many, array_type->GetSentinel(),
          m_type->IsAllowZero(), m_type->GetPointerAlign(),
          m_type->GetAddressSpace(), m_type->IsConst(), m_type->IsVolatile(),
          array_type->GetChildType());
      if (ValueObjectZig *zig_valobj = ValueObjectZig::dyn_cast(&m_backend))
        if (ZigPointer *zig_value = llvm::dyn_cast_if_present<ZigPointer>(
                zig_valobj->UnwrapZigValue().second)) {
          lldb::ValueObjectSP ptr_valobj = ValueObjectZig::Create(
              exe_scope,
              type_system->GetPointer(
                  llvm::cast<ZigPointerType>(type_system->UnwrapType(ptr_type)),
                  zig_value->GetPointee(), zig_value->GetOffset()));
          ptr_valobj->SetName(ConstString("ptr"));
          return ptr_valobj;
        }
      lldb::addr_t ptr_addr = m_backend.GetPointerValue();
      if (ptr_addr == LLDB_INVALID_ADDRESS)
        return nullptr;
      return ValueObject::CreateValueObjectFromAddress(
          "ptr", ptr_addr, exe_scope, ptr_type, false);
    }
    case 1: {
      ZigIntType *len_type = llvm::cast<ZigIntType>(
          type_system->UnwrapType(type_system->GetSizeType(false)));
      lldb::ValueObjectSP len_valobj = ValueObjectZig::Create(
          exe_scope,
          type_system->GetInt(len_type, llvm::APInt(len_type->GetBitSize(),
                                                    array_type->GetLength())));
      len_valobj->SetName(ConstString("len"));
      return len_valobj;
    }
    }
    return nullptr;
  }

private:
  ZigPointerType *m_type;
};

class ZigSliceSyntheticFrontEnd final : public SyntheticChildrenFrontEnd {
public:
  ZigSliceSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_ptr(nullptr), m_len(nullptr) {}

  lldb::ChildCacheState Update() override {
    m_ptr = m_backend.GetChildMemberWithName("ptr").get();
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
    if (!m_ptr)
      return ValueObjectConstResult::Create(
          nullptr, Status::FromError(
                       llvm::createStringError("could not read ptr field")));
    return m_ptr->GetSyntheticArrayMember(idx, true);
  }

private:
  ValueObject *m_ptr;
  ValueObject *m_len;
};

class ZigErrorUnionSyntheticFrontEnd final : public SyntheticChildrenFrontEnd {
public:
  ZigErrorUnionSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_child(nullptr) {}

  lldb::ChildCacheState Update() override {
    bool valid = false;
    if (ValueObject *error = m_backend.GetChildMemberWithName("error").get()) {
      bool is_error = error->GetValueAsUnsigned(0, &valid);
      if (valid) {
        if (is_error)
          m_child = error;
        else
          m_child = m_backend.GetChildMemberWithName("value").get();
      }
    }
    if (!valid)
      m_child = nullptr;
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    if (!m_child)
      return llvm::createStringError("could not read child field");
    return m_child ? 1 : 0;
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    if (m_child && name == m_child->GetName())
      return 0;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    if (idx < *num_children_or_err)
      switch (idx) {
      case 0:
        return m_child->GetSP();
      default:
        llvm_unreachable("invalid index");
      }
    return nullptr;
  }

private:
  ValueObject *m_child;
};

class ZigOptionalSyntheticFrontEnd final : public SyntheticChildrenFrontEnd {
public:
  ZigOptionalSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_child(nullptr) {}

  lldb::ChildCacheState Update() override {
    bool valid = false;
    if (auto has_value_child = m_backend.GetChildMemberWithName("has_value"))
      if (has_value_child->GetValueAsUnsigned(0, &valid) && valid)
        m_child = m_backend.GetChildMemberWithName("?").get();
    if (!valid)
      m_child = nullptr;
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return m_child; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_child ? 1 : 0;
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    if (m_child && name == m_child->GetName())
      return 0;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    if (idx < *num_children_or_err)
      switch (idx) {
      case 0:
        return m_child->GetSP();
      default:
        llvm_unreachable("invalid index");
      }
    return nullptr;
  }

private:
  ValueObject *m_child;
};

class ZigTaggedUnionSyntheticFrontEnd final : public SyntheticChildrenFrontEnd {
public:
  ZigTaggedUnionSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
      : SyntheticChildrenFrontEnd(*valobj_sp), m_child(nullptr) {}

  lldb::ChildCacheState Update() override {
    m_child = nullptr;
    std::string tag;
    if (auto tag_child = m_backend.GetChildAtIndex(0);
        tag_child && tag_child->GetName() == ".tag" &&
        tag_child->GetValueAsCString(lldb::eFormatEnum, tag)) {
      llvm::StringRef member(tag);
      if (member.consume_front("."))
        m_child = m_backend.GetChildMemberWithName(member).get();
    }
    return lldb::eRefetch;
  }

  bool MightHaveChildren() override { return true; }

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_child ? 1 : 0;
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    if (m_child && name == m_child->GetName())
      return 0;
    return UINT32_MAX;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    auto num_children_or_err = CalculateNumChildren();
    if (auto err = num_children_or_err.takeError())
      return ValueObjectConstResult::Create(nullptr,
                                            Status::FromError(std::move(err)));
    if (idx < *num_children_or_err)
      switch (idx) {
      case 0:
        return m_child->GetSP();
      default:
        llvm_unreachable("invalid index");
      }
    return nullptr;
  }

private:
  ValueObject *m_child;
};

} // namespace

bool formatters::ZigAliasSummaryProvider(ValueObject &valobj, Stream &s,
                                         const TypeSummaryOptions &) {
  auto [type_system, zig_value] =
      static_cast<ValueObjectZig &>(valobj).UnwrapZigValue();
  if (!zig_value)
    return false;
  return type_system->DumpValue(zig_value, s);
}

bool formatters::ZigComptimeSummaryProvider(ValueObject &valobj, Stream &s,
                                            const TypeSummaryOptions &) {
  auto [type_system, zig_value] =
      static_cast<ValueObjectZig &>(valobj).UnwrapZigValue();
  if (!zig_value)
    return false;
  if (ZigType *zig_type = llvm::dyn_cast<ZigType>(zig_value))
    return type_system->DumpTypeDecl(zig_type->AsOpaqueType(), s);
  return type_system->DumpValue(zig_value, s);
}

SyntheticChildrenFrontEnd *formatters::ZigArrayPointerSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigArrayPointerSyntheticFrontEnd(valobj_sp) : nullptr;
}

SyntheticChildrenFrontEnd *
formatters::ZigSliceSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                             lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigSliceSyntheticFrontEnd(valobj_sp) : nullptr;
}

SyntheticChildrenFrontEnd *formatters::ZigErrorUnionSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigErrorUnionSyntheticFrontEnd(valobj_sp) : nullptr;
}

SyntheticChildrenFrontEnd *
formatters::ZigOptionalSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                                lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigOptionalSyntheticFrontEnd(valobj_sp) : nullptr;
}

SyntheticChildrenFrontEnd *formatters::ZigTaggedUnionSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  return valobj_sp ? new ZigTaggedUnionSyntheticFrontEnd(valobj_sp) : nullptr;
}
