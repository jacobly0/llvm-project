//===-- TypeFormat.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeFormat.h"




#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/StreamString.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

TypeFormatImpl::TypeFormatImpl(const Flags &flags) : m_flags(flags) {}

TypeFormatImpl::~TypeFormatImpl() = default;

TypeFormatImpl_Format::TypeFormatImpl_Format(lldb::Format f,
                                             const TypeFormatImpl::Flags &flags)
    : TypeFormatImpl(flags), m_format(f) {}

TypeFormatImpl_Format::~TypeFormatImpl_Format() = default;

bool TypeFormatImpl_Format::FormatObject(ValueObject *valobj,
                                         std::string &dest) const {
  if (!valobj)
    return false;
  bool format_cstring = GetFormat() == eFormatCString;
  if (format_cstring || valobj->CanProvideValue()) {
    Value &value(valobj->GetValue());
    const Value::ContextType context_type = value.GetContextType();
    ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
    DataExtractor data;

    if (context_type == Value::ContextType::RegisterInfo) {
      if (format_cstring && !valobj->CanProvideValue())
        return false;
      const RegisterInfo *reg_info = value.GetRegisterInfo();
      if (reg_info) {
        Status error;
        valobj->GetData(data, error);
        if (error.Fail())
          return false;

        StreamString reg_sstr;
        DumpDataExtractor(data, &reg_sstr, 0, GetFormat(), reg_info->byte_size,
                          1, UINT32_MAX, LLDB_INVALID_ADDRESS, 0, 0,
                          exe_ctx.GetBestExecutionContextScope());
        dest = std::string(reg_sstr.GetString());
      }
    } else if (CompilerType compiler_type = value.GetCompilerType()) {
      // put custom bytes to display in the DataExtractor to override the
      // default value logic
      if (format_cstring) {
        uint64_t length = UINT64_MAX;
        char terminator = 0;
        if (ValueObject *ptr_valobj =
                compiler_type.GetStringPointer(valobj, &length, &terminator)) {
          // if we are dumping a pointer as a c-string, get the pointee data
          // as a string
          if (Target *target = exe_ctx.GetTargetPtr()) {
            size_t max_len = target->GetMaximumSizeOfStringSummary();
            if (max_len > length)
              max_len = length;
            AddressType address_type = eAddressTypeInvalid;
            if (addr_t ptr_val = ptr_valobj->GetPointerValue(&address_type);
                ptr_val != LLDB_INVALID_ADDRESS) {
              Status error;
              WritableDataBufferSP buffer_sp(
                  new DataBufferHeap(max_len + 1, 0));
              switch (address_type) {
              case eAddressTypeInvalid:
                break;
              case eAddressTypeHost: {
                const uint8_t *host_ptr =
                    reinterpret_cast<const uint8_t *>(ptr_val);
                if (length == UINT64_MAX)
                  if (const uint8_t *host_terminator =
                          static_cast<const uint8_t *>(
                              memchr(host_ptr, terminator, max_len)))
                    max_len = host_terminator - host_ptr;
                memcpy(buffer_sp->GetBytes(), host_ptr, max_len);
                break;
              }
              case eAddressTypeLoad:
              case eAddressTypeFile: {
                Address address(LLDB_INVALID_ADDRESS);
                switch (address_type) {
                default:
                  llvm_unreachable("already checked");
                case eAddressTypeLoad:
                  address = ptr_val;
                  break;
                case eAddressTypeFile:
                  if (ModuleSP module = ptr_valobj->GetModule())
                    if (ObjectFile *objfile = module->GetObjectFile()) {
                      address = Address(ptr_val, objfile->GetSectionList());
                      addr_t load_addr = address.GetLoadAddress(target);
                      if (load_addr != LLDB_INVALID_ADDRESS)
                        address = load_addr;
                    }
                  break;
                }
                if (address == LLDB_INVALID_ADDRESS)
                  break;
                if (length == UINT64_MAX)
                  max_len = target->ReadCStringFromMemory(
                      address, (char *)buffer_sp->GetBytes(), max_len, error,
                      false, terminator);
                else
                  max_len = target->ReadMemory(address, buffer_sp->GetBytes(),
                                               max_len, error);
                break;
              }
              }
              if (error.Success())
                data.SetData(buffer_sp, 0, max_len + 1);
            }
          }
        } else if (valobj->CanProvideValue()) {
          Status error;
          valobj->GetData(data, error);
          if (error.Fail())
            return false;
          char terminator = '\0';
          data.Append(&terminator, 1);
        } else
          return false;
      } else {
        Status error;
        valobj->GetData(data, error);
        if (error.Fail())
          return false;
      }

      ExecutionContextScope *exe_scope = exe_ctx.GetBestExecutionContextScope();
      std::optional<uint64_t> size = compiler_type.GetByteSize(exe_scope);
      if (!size)
        return false;
      StreamString sstr;
      compiler_type.DumpTypeValue(
          &sstr,                        // The stream to use for display
          GetFormat(),                  // Format to display this type with
          data,                         // Data to extract from
          0,                            // Byte offset into "m_data"
          *size,                        // Byte size of item in "m_data"
          valobj->GetBitfieldBitSize(), // Bitfield bit size
          value.GetBitOffset() +
              valobj->GetBitfieldBitOffset(), // Bitfield bit offset
          exe_scope);
      // Given that we do not want to set the ValueObject's m_error for a
      // formatting error (or else we wouldn't be able to reformat until a
      // next update), an empty string is treated as a "false" return from
      // here, but that's about as severe as we get
      // CompilerType::DumpTypeValue() should always return something, even
      // if that something is an error message
      dest = std::string(sstr.GetString());
    }
    return !dest.empty();
  }
  return false;
}

std::string TypeFormatImpl_Format::GetDescription() {
  StreamString sstr;
  sstr.Printf("%s%s%s%s", FormatManager::GetFormatAsCString(GetFormat()),
              Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "");
  return std::string(sstr.GetString());
}

TypeFormatImpl_EnumType::TypeFormatImpl_EnumType(
    ConstString type_name, const TypeFormatImpl::Flags &flags)
    : TypeFormatImpl(flags), m_enum_type(type_name), m_types() {}

TypeFormatImpl_EnumType::~TypeFormatImpl_EnumType() = default;

bool TypeFormatImpl_EnumType::FormatObject(ValueObject *valobj,
                                           std::string &dest) const {
  dest.clear();
  if (!valobj)
    return false;
  if (!valobj->CanProvideValue())
    return false;
  ProcessSP process_sp;
  TargetSP target_sp;
  void *valobj_key = (process_sp = valobj->GetProcessSP()).get();
  if (!valobj_key)
    valobj_key = (target_sp = valobj->GetTargetSP()).get();
  else
    target_sp = process_sp->GetTarget().shared_from_this();
  if (!valobj_key)
    return false;
  auto iter = m_types.find(valobj_key), end = m_types.end();
  CompilerType valobj_enum_type;
  if (iter == end) {
    // probably a redundant check
    if (!target_sp)
      return false;
    const ModuleList &images(target_sp->GetImages());
    TypeQuery query(m_enum_type.GetStringRef());
    TypeResults results;
    images.FindTypes(nullptr, query, results);
    if (results.GetTypeMap().Empty())
      return false;
    for (lldb::TypeSP type_sp : results.GetTypeMap().Types()) {
      if (!type_sp)
        continue;
      if ((type_sp->GetForwardCompilerType().GetTypeInfo() &
           eTypeIsEnumeration) == eTypeIsEnumeration) {
        valobj_enum_type = type_sp->GetFullCompilerType();
        m_types.emplace(valobj_key, valobj_enum_type);
        break;
      }
    }
  } else
    valobj_enum_type = iter->second;
  if (!valobj_enum_type.IsValid())
    return false;
  DataExtractor data;
  Status error;
  valobj->GetData(data, error);
  if (error.Fail())
    return false;
  ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
  StreamString sstr;
  valobj_enum_type.DumpTypeValue(&sstr, lldb::eFormatEnum, data, 0,
                                 data.GetByteSize(), 0, 0,
                                 exe_ctx.GetBestExecutionContextScope());
  if (!sstr.GetString().empty())
    dest = std::string(sstr.GetString());
  return !dest.empty();
}

std::string TypeFormatImpl_EnumType::GetDescription() {
  StreamString sstr;
  sstr.Printf("as type %s%s%s%s", m_enum_type.AsCString("<invalid type>"),
              Cascades() ? "" : " (not cascading)",
              SkipsPointers() ? " (skip pointers)" : "",
              SkipsReferences() ? " (skip references)" : "");
  return std::string(sstr.GetString());
}
