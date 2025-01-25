//===-- DumpDataExtractor.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/DumpDataExtractor.h"

#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/MemoryRegionInfo.h"
#include "lldb/Target/MemoryTagManager.h"
#include "lldb/Target/MemoryTagMap.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

#include <limits>
#include <memory>
#include <string>

#include <cassert>
#include <cctype>
#include <cinttypes>
#include <cmath>

#include <bitset>
#include <optional>
#include <sstream>

using namespace lldb_private;
using namespace lldb;

#define NON_PRINTABLE_CHAR '.'

static std::optional<llvm::APInt> GetAPInt(const DataExtractor &data,
                                           lldb::offset_t *offset_ptr,
                                           lldb::offset_t byte_size) {
  if (byte_size == 0)
    return std::nullopt;

  llvm::SmallVector<uint64_t, 2> uint64_array;
  lldb::offset_t bytes_left = byte_size;
  uint64_t u64;
  const lldb::ByteOrder byte_order = data.GetByteOrder();
  if (byte_order == lldb::eByteOrderLittle) {
    while (bytes_left > 0) {
      if (bytes_left >= 8) {
        u64 = data.GetU64(offset_ptr);
        bytes_left -= 8;
      } else {
        u64 = data.GetMaxU64(offset_ptr, (uint32_t)bytes_left);
        bytes_left = 0;
      }
      uint64_array.push_back(u64);
    }
    return llvm::APInt(byte_size * 8, llvm::ArrayRef<uint64_t>(uint64_array));
  } else if (byte_order == lldb::eByteOrderBig) {
    lldb::offset_t be_offset = *offset_ptr + byte_size;
    lldb::offset_t temp_offset;
    while (bytes_left > 0) {
      if (bytes_left >= 8) {
        be_offset -= 8;
        temp_offset = be_offset;
        u64 = data.GetU64(&temp_offset);
        bytes_left -= 8;
      } else {
        be_offset -= bytes_left;
        temp_offset = be_offset;
        u64 = data.GetMaxU64(&temp_offset, (uint32_t)bytes_left);
        bytes_left = 0;
      }
      uint64_array.push_back(u64);
    }
    *offset_ptr += byte_size;
    return llvm::APInt(byte_size * 8, llvm::ArrayRef<uint64_t>(uint64_array));
  }
  return std::nullopt;
}

static lldb::offset_t DumpAPInt(lldb::LanguageType lang, Stream *s,
                                const DataExtractor &data,
                                lldb::offset_t offset, lldb::offset_t byte_size,
                                bool is_signed, unsigned radix) {
  std::optional<llvm::APInt> apint = GetAPInt(data, &offset, byte_size);
  if (apint) {
    std::string apint_str = toString(*apint, radix, is_signed);
    switch (radix) {
    case 2:
      *s << "0b";
      break;
    case 8:
      *s << (lang == eLanguageTypeZig ? "0o" : "0");
      break;
    case 10:
      break;
    }
    s->Write(apint_str.c_str(), apint_str.size());
  }
  return offset;
}

/// Dumps decoded instructions to a stream.
static lldb::offset_t DumpInstructions(const DataExtractor &DE, Stream *s,
                                       ExecutionContextScope *exe_scope,
                                       offset_t start_offset,
                                       uint64_t base_addr,
                                       size_t number_of_instructions) {
  offset_t offset = start_offset;

  TargetSP target_sp;
  if (exe_scope)
    target_sp = exe_scope->CalculateTarget();
  if (target_sp) {
    DisassemblerSP disassembler_sp(Disassembler::FindPlugin(
        target_sp->GetArchitecture(), target_sp->GetDisassemblyFlavor(),
        target_sp->GetDisassemblyCPU(), target_sp->GetDisassemblyFeatures(),
        nullptr));
    if (disassembler_sp) {
      lldb::addr_t addr = base_addr + start_offset;
      lldb_private::Address so_addr;
      bool data_from_file = true;
      if (target_sp->ResolveLoadAddress(addr, so_addr)) {
        data_from_file = false;
      } else {
        if (!target_sp->HasLoadedSections() ||
            !target_sp->GetImages().ResolveFileAddress(addr, so_addr))
          so_addr.SetRawAddress(addr);
      }

      size_t bytes_consumed = disassembler_sp->DecodeInstructions(
          so_addr, DE, start_offset, number_of_instructions, false,
          data_from_file);

      if (bytes_consumed) {
        offset += bytes_consumed;
        const bool show_address = base_addr != LLDB_INVALID_ADDRESS;
        const bool show_bytes = false;
        const bool show_control_flow_kind = false;
        ExecutionContext exe_ctx;
        exe_scope->CalculateExecutionContext(exe_ctx);
        disassembler_sp->GetInstructionList().Dump(
            s, show_address, show_bytes, show_control_flow_kind, &exe_ctx);
      }
    }
  } else
    s->Printf("invalid target");

  return offset;
}

/// Prints the specific escape sequence of the given character to the stream.
/// If the character doesn't have a known specific escape sequence (e.g., '\a',
/// '\n' but not generic escape sequences such as'\x12'), this function will
/// not modify the stream and return false.
static bool TryDumpSpecialEscapedChar(lldb::LanguageType lang, Stream &s,
                                      const char c) {
  switch (c) {
  case '\033':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    // Common non-standard escape code for 'escape'.
    s.Printf("\\e");
    return true;
  case '\a':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\a");
    return true;
  case '\b':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\b");
    return true;
  case '\f':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\f");
    return true;
  case '\n':
    s.Printf("\\n");
    return true;
  case '\r':
    s.Printf("\\r");
    return true;
  case '\t':
    s.Printf("\\t");
    return true;
  case '\v':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\v");
    return true;
  case '\0':
    if (lang == lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\0");
    return true;
  case '\\':
    if (lang != lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\\\");
    return true;
  case '\'':
    if (lang != lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\\'");
    return true;
  case '\"':
    if (lang != lldb::eLanguageTypeZig)
      return false;
    s.Printf("\\\"");
    return true;
  default:
    return false;
  }
}

/// Dump the character to a stream. A character that is not printable will be
/// represented by its escape sequence.
static void DumpCharacter(lldb::LanguageType lang, Stream &s, const char c) {
  if (TryDumpSpecialEscapedChar(lang, s, c))
    return;
  if (llvm::isPrint(c)) {
    s.PutChar(c);
    return;
  }
  s.Printf("\\x%2.2hhx", c);
}

static std::optional<MemoryTagMap>
GetMemoryTags(lldb::addr_t addr, size_t length,
              ExecutionContextScope *exe_scope) {
  assert(addr != LLDB_INVALID_ADDRESS);

  if (!exe_scope)
    return std::nullopt;

  TargetSP target_sp = exe_scope->CalculateTarget();
  if (!target_sp)
    return std::nullopt;

  ProcessSP process_sp = target_sp->CalculateProcess();
  if (!process_sp)
    return std::nullopt;

  llvm::Expected<const MemoryTagManager *> tag_manager_or_err =
      process_sp->GetMemoryTagManager();
  if (!tag_manager_or_err) {
    llvm::consumeError(tag_manager_or_err.takeError());
    return std::nullopt;
  }

  MemoryRegionInfos memory_regions;
  // Don't check return status, list will be just empty if an error happened.
  process_sp->GetMemoryRegions(memory_regions);

  llvm::Expected<std::vector<MemoryTagManager::TagRange>> tagged_ranges_or_err =
      (*tag_manager_or_err)
          ->MakeTaggedRanges(addr, addr + length, memory_regions);
  // Here we know that our range will not be inverted but we must still check
  // for an error.
  if (!tagged_ranges_or_err) {
    llvm::consumeError(tagged_ranges_or_err.takeError());
    return std::nullopt;
  }
  if (tagged_ranges_or_err->empty())
    return std::nullopt;

  MemoryTagMap memory_tag_map(*tag_manager_or_err);
  for (const MemoryTagManager::TagRange &range : *tagged_ranges_or_err) {
    llvm::Expected<std::vector<lldb::addr_t>> tags_or_err =
        process_sp->ReadMemoryTags(range.GetRangeBase(), range.GetByteSize());

    if (tags_or_err)
      memory_tag_map.InsertTags(range.GetRangeBase(), *tags_or_err);
    else
      llvm::consumeError(tags_or_err.takeError());
  }

  if (memory_tag_map.Empty())
    return std::nullopt;

  return memory_tag_map;
}

static void printMemoryTags(const DataExtractor &DE, Stream *s,
                            lldb::addr_t addr, size_t len,
                            const std::optional<MemoryTagMap> &memory_tag_map) {
  std::vector<std::optional<lldb::addr_t>> tags =
      memory_tag_map->GetTags(addr, len);

  // Only print if there is at least one tag for this line
  if (tags.empty())
    return;

  s->Printf(" (tag%s:", tags.size() > 1 ? "s" : "");
  // Some granules may not be tagged but print something for them
  // so that the ordering remains intact.
  for (auto tag : tags) {
    if (tag)
      s->Printf(" 0x%" PRIx64, *tag);
    else
      s->PutCString(" <no tag>");
  }
  s->PutCString(")");
}

static const llvm::fltSemantics &GetFloatSemantics(const TargetSP &target_sp,
                                                   lldb::LanguageType lang,
                                                   size_t byte_size) {
  auto type_system_or_err = target_sp->GetScratchTypeSystemForLanguage(lang);
  if (!type_system_or_err)
    llvm::consumeError(type_system_or_err.takeError());
  else if (auto ts = *type_system_or_err)
    return ts->GetFloatTypeSemantics(byte_size);
  // No target, just make a reasonable guess
  switch(byte_size) {
    case 2:
      return llvm::APFloat::IEEEhalf();
    case 4:
      return llvm::APFloat::IEEEsingle();
    case 8:
      return llvm::APFloat::IEEEdouble();
  }
  return llvm::APFloat::Bogus();
}

static bool DumpFloat(const DataExtractor &DE, Stream *s, offset_t &offset,
                      size_t item_byte_size, ExecutionContextScope *exe_scope,
                      const llvm::fltSemantics *&flt_semantics) {
  TargetSP target_sp;
  if (exe_scope) {
    target_sp = exe_scope->CalculateTarget();
    if (!flt_semantics) {
      SourceLanguage lang;
      if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
        lang = frame_sp->GuessLanguage();
      else if (target_sp)
        lang = target_sp->GetLanguage();
      flt_semantics =
          &GetFloatSemantics(target_sp, lang.AsLanguageType(), item_byte_size);
    }
  }

  std::optional<unsigned> format_max_padding;
  if (target_sp)
    format_max_padding = target_sp->GetMaxZeroPaddingInFloatFormat();

  // Show full precision when printing float values
  const unsigned format_precision = 0;

  // Recalculate the byte size in case of a difference. This is possible
  // when item_byte_size is 16 (128-bit), because you could get back the
  // x87DoubleExtended semantics which has a byte size of 10 (80-bit).
  const size_t semantics_byte_size =
      (llvm::APFloat::getSizeInBits(*flt_semantics) + 7) / 8;
  if (std::optional<llvm::APInt> apint =
          GetAPInt(DE, &offset, semantics_byte_size)) {
    llvm::APFloat apfloat(*flt_semantics, *apint);
    llvm::SmallVector<char, 256> sv;
    if (format_max_padding)
      apfloat.toString(sv, format_precision, *format_max_padding);
    else
      apfloat.toString(sv, format_precision);
    s->AsRawOstream() << sv;
    return true;
  }
  s->Format("error: unsupported byte size ({0}) for float format",
            item_byte_size);
  return false;
}

lldb::offset_t lldb_private::DumpDataExtractor(
    const DataExtractor &DE, Stream *s, offset_t start_offset,
    lldb::Format item_format, size_t item_byte_size, size_t item_count,
    size_t num_per_line, uint64_t base_addr,
    uint32_t item_bit_size,   // If zero, this is not a bitfield value, if
                              // non-zero, the value is a bitfield
    uint32_t item_bit_offset, // If "item_bit_size" is non-zero, this is the
                              // shift amount to apply to a bitfield
    ExecutionContextScope *exe_scope, bool show_memory_tags,
    const llvm::fltSemantics *flt_semantics) {
  if (s == nullptr)
    return start_offset;

  if (item_format == eFormatPointer) {
    if (item_byte_size != 4 && item_byte_size != 8)
      item_byte_size = s->GetAddressByteSize();
  }

  offset_t offset = start_offset;

  std::optional<MemoryTagMap> memory_tag_map;
  if (show_memory_tags && base_addr != LLDB_INVALID_ADDRESS)
    memory_tag_map =
        GetMemoryTags(base_addr, DE.GetByteSize() - offset, exe_scope);

  if (item_format == eFormatInstruction)
    return DumpInstructions(DE, s, exe_scope, start_offset, base_addr,
                            item_count);

  if ((item_format == eFormatOSType || item_format == eFormatAddressInfo) &&
      item_byte_size > 8)
    item_format = eFormatHex;

  lldb::offset_t line_start_offset = start_offset;
  for (uint32_t count = 0; DE.ValidOffset(offset) && count < item_count;
       ++count) {
    // If we are at the beginning or end of a line
    // Note that the last line is handled outside this for loop.
    if ((count % num_per_line) == 0) {
      // If we are at the end of a line
      if (count > 0) {
        if (item_format == eFormatBytesWithASCII &&
            offset > line_start_offset) {
          s->Printf("%*s",
                    static_cast<int>(
                        (num_per_line - (offset - line_start_offset)) * 3 + 2),
                    "");
          DumpDataExtractor(DE, s, line_start_offset, eFormatCharPrintable, 1,
                            offset - line_start_offset, SIZE_MAX,
                            LLDB_INVALID_ADDRESS, 0, 0);
        }

        if (base_addr != LLDB_INVALID_ADDRESS && memory_tag_map) {
          size_t line_len = offset - line_start_offset;
          lldb::addr_t line_base =
              base_addr +
              (offset - start_offset - line_len) / DE.getTargetByteSize();
          printMemoryTags(DE, s, line_base, line_len, memory_tag_map);
        }

        s->EOL();
      }
      if (base_addr != LLDB_INVALID_ADDRESS)
        s->Printf("0x%8.8" PRIx64 ": ",
                  (uint64_t)(base_addr +
                             (offset - start_offset) / DE.getTargetByteSize()));

      line_start_offset = offset;
    } else if (item_format != eFormatChar &&
               item_format != eFormatCharPrintable &&
               item_format != eFormatCharArray && count > 0) {
      s->PutChar(' ');
    }

    switch (item_format) {
    case eFormatBoolean:
      if (item_byte_size <= 8)
        s->Printf("%s", DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                             item_bit_size, item_bit_offset)
                            ? "true"
                            : "false");
      else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for boolean format",
                  (uint64_t)item_byte_size);
        return offset;
      }
      break;

    case eFormatBinary:
      if (item_byte_size <= 8) {
        uint64_t uval64 = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                               item_bit_size, item_bit_offset);
        // Avoid std::bitset<64>::to_string() since it is missing in earlier
        // C++ libraries
        std::string binary_value(64, '0');
        std::bitset<64> bits(uval64);
        for (uint32_t i = 0; i < 64; ++i)
          if (bits[i])
            binary_value[64 - 1 - i] = '1';
        if (item_bit_size > 0)
          s->Printf("0b%s", binary_value.c_str() + 64 - item_bit_size);
        else if (item_byte_size > 0 && item_byte_size <= 8)
          s->Printf("0b%s", binary_value.c_str() + 64 - item_byte_size * 8);
      } else {
        SourceLanguage lang;
        if (exe_scope) {
          if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
            lang = frame_sp->GuessLanguage();
          else if (TargetSP target_sp = exe_scope->CalculateTarget())
            lang = target_sp->GetLanguage();
        }

        const bool is_signed = false;
        const unsigned radix = 2;
        offset = DumpAPInt(lang.AsLanguageType(), s, DE, offset, item_byte_size,
                           is_signed, radix);
      }
      break;

    case eFormatBytes:
    case eFormatBytesWithASCII:
      for (uint32_t i = 0; i < item_byte_size; ++i) {
        s->Printf("%2.2x", DE.GetU8(&offset));
      }

      // Put an extra space between the groups of bytes if more than one is
      // being dumped in a group (item_byte_size is more than 1).
      if (item_byte_size > 1)
        s->PutChar(' ');
      break;

    case eFormatChar:
    case eFormatCharPrintable:
    case eFormatCharArray: {
      // Reject invalid item_byte_size.
      if (item_byte_size > 8) {
        s->Printf("error: unsupported byte size (%" PRIu64 ") for char format",
                  (uint64_t)item_byte_size);
        return offset;
      }

      SourceLanguage lang;
      if (exe_scope) {
        if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
          lang = frame_sp->GuessLanguage();
        else if (TargetSP target_sp = exe_scope->CalculateTarget())
          lang = target_sp->GetLanguage();
      }

      // If we are only printing one character surround it with single quotes
      if (item_count == 1 && item_format == eFormatChar)
        s->PutChar('\'');

      const uint64_t ch = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                               item_bit_size, item_bit_offset);
      if (llvm::isPrint(ch))
        DumpCharacter(lang.AsLanguageType(), *s, ch);
      else if (item_format != eFormatCharPrintable) {
        if (!TryDumpSpecialEscapedChar(lang.AsLanguageType(), *s, ch)) {
          if (item_byte_size == 1)
            s->Printf("\\x%2.2x", (uint8_t)ch);
          else
            s->Printf("%" PRIu64, ch);
        }
      } else {
        s->PutChar(NON_PRINTABLE_CHAR);
      }

      // If we are only printing one character surround it with single quotes
      if (item_count == 1 && item_format == eFormatChar)
        s->PutChar('\'');
    } break;

    case eFormatEnum: // Print enum value as a signed integer when we don't get
                      // the enum type
    case eFormatDecimal:
      if (item_byte_size <= 8)
        s->Printf("%" PRId64,
                  DE.GetMaxS64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      else {
        SourceLanguage lang;
        if (exe_scope) {
          if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
            lang = frame_sp->GuessLanguage();
          else if (TargetSP target_sp = exe_scope->CalculateTarget())
            lang = target_sp->GetLanguage();
        }

        const bool is_signed = true;
        const unsigned radix = 10;
        offset = DumpAPInt(lang.AsLanguageType(), s, DE, offset, item_byte_size,
                           is_signed, radix);
      }
      break;

    case eFormatUnsigned:
      if (item_byte_size <= 8)
        s->Printf("%" PRIu64,
                  DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      else {
        SourceLanguage lang;
        if (exe_scope) {
          if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
            lang = frame_sp->GuessLanguage();
          else if (TargetSP target_sp = exe_scope->CalculateTarget())
            lang = target_sp->GetLanguage();
        }

        const bool is_signed = false;
        const unsigned radix = 10;
        offset = DumpAPInt(lang.AsLanguageType(), s, DE, offset, item_byte_size,
                           is_signed, radix);
      }
      break;

    case eFormatOctal: {
      SourceLanguage lang;
      if (exe_scope) {
        if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
          lang = frame_sp->GuessLanguage();
        else if (TargetSP target_sp = exe_scope->CalculateTarget())
          lang = target_sp->GetLanguage();
      }

      if (item_byte_size <= 8) {
        s->Printf("%s%" PRIo64,
                  lang.AsLanguageType() == eLanguageTypeZig ? "0o" : "0",
                  DE.GetMaxS64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset));
      } else {
        const bool is_signed = false;
        const unsigned radix = 8;
        offset = DumpAPInt(lang.AsLanguageType(), s, DE, offset, item_byte_size,
                           is_signed, radix);
      }
    } break;

    case eFormatOSType: {
      SourceLanguage lang;
      if (exe_scope) {
        if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
          lang = frame_sp->GuessLanguage();
        else if (TargetSP target_sp = exe_scope->CalculateTarget())
          lang = target_sp->GetLanguage();
      }

      uint64_t uval64 = DE.GetMaxU64Bitfield(&offset, item_byte_size,
                                             item_bit_size, item_bit_offset);
      s->PutChar('\'');
      for (uint32_t i = 0; i < item_byte_size; ++i) {
        uint8_t ch = (uint8_t)(uval64 >> ((item_byte_size - i - 1) * 8));
        DumpCharacter(lang.AsLanguageType(), *s, ch);
      }
      s->PutChar('\'');
    } break;

    case eFormatCString: {
      SourceLanguage lang;
      if (exe_scope) {
        if (StackFrameSP frame_sp = exe_scope->CalculateStackFrame())
          lang = frame_sp->GuessLanguage();
        else if (TargetSP target_sp = exe_scope->CalculateTarget())
          lang = target_sp->GetLanguage();
      }

      llvm::ArrayRef<uint8_t> data = DE.GetData();
      if (data.empty()) {
        if (Language *lang_plugin = Language::FindPlugin(lang.AsLanguageType()))
          *s << lang_plugin->GetNilReferenceSummaryString();
        else
          *s << "NULL";
        offset = LLDB_INVALID_OFFSET;
      } else {
        s->PutChar('\"');
        for (uint8_t c : data.drop_back(1))
          DumpCharacter(lang.AsLanguageType(), *s, c);
        s->PutChar('\"');
      }
    } break;

    case eFormatPointer:
      DumpAddress(s->AsRawOstream(),
                  DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                       item_bit_offset),
                  sizeof(addr_t));
      break;

    case eFormatComplexInteger: {
      size_t complex_int_byte_size = item_byte_size / 2;

      if (complex_int_byte_size > 0 && complex_int_byte_size <= 8) {
        s->Printf("%" PRIu64,
                  DE.GetMaxU64Bitfield(&offset, complex_int_byte_size, 0, 0));
        s->Printf(" + %" PRIu64 "i",
                  DE.GetMaxU64Bitfield(&offset, complex_int_byte_size, 0, 0));
      } else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for complex integer format",
                  (uint64_t)item_byte_size);
        return offset;
      }
    } break;

    case eFormatComplex:
      if (!DumpFloat(DE, s, offset, item_byte_size / 2, exe_scope,
                     flt_semantics))
        return offset;
      s->PutCString(" + ");
      if (!DumpFloat(DE, s, offset, item_byte_size / 2, exe_scope,
                     flt_semantics))
        return offset;
      s->PutChar('i');
      break;

    default:
    case eFormatDefault:
    case eFormatHex:
    case eFormatHexUppercase: {
      bool wantsuppercase = (item_format == eFormatHexUppercase);
      switch (item_byte_size) {
      case 1:
      case 2:
      case 4:
      case 8:
        if (Target::GetGlobalProperties()
                .ShowHexVariableValuesWithLeadingZeroes()) {
          s->Printf(wantsuppercase ? "0x%*.*" PRIX64 : "0x%*.*" PRIx64,
                    (int)(2 * item_byte_size), (int)(2 * item_byte_size),
                    DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                         item_bit_offset));
        } else {
          s->Printf(wantsuppercase ? "0x%" PRIX64 : "0x%" PRIx64,
                    DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                         item_bit_offset));
        }
        break;
      default: {
        assert(item_bit_size == 0 && item_bit_offset == 0);
        const uint8_t *bytes =
            (const uint8_t *)DE.GetData(&offset, item_byte_size);
        if (bytes) {
          s->PutCString("0x");
          uint32_t idx;
          if (DE.GetByteOrder() == eByteOrderBig) {
            for (idx = 0; idx < item_byte_size; ++idx)
              s->Printf(wantsuppercase ? "%2.2X" : "%2.2x", bytes[idx]);
          } else {
            for (idx = 0; idx < item_byte_size; ++idx)
              s->Printf(wantsuppercase ? "%2.2X" : "%2.2x",
                        bytes[item_byte_size - 1 - idx]);
          }
        }
      } break;
      }
    } break;

    case eFormatFloat:
      if (!DumpFloat(DE, s, offset, item_byte_size, exe_scope, flt_semantics))
        return offset;
      break;

    case eFormatUnicode16:
      s->Printf("U+%4.4x", DE.GetU16(&offset));
      break;

    case eFormatUnicode32:
      s->Printf("U+0x%8.8x", DE.GetU32(&offset));
      break;

    case eFormatAddressInfo: {
      addr_t addr = DE.GetMaxU64Bitfield(&offset, item_byte_size, item_bit_size,
                                         item_bit_offset);
      s->Printf("0x%*.*" PRIx64, (int)(2 * item_byte_size),
                (int)(2 * item_byte_size), addr);
      if (exe_scope) {
        TargetSP target_sp(exe_scope->CalculateTarget());
        lldb_private::Address so_addr;
        if (target_sp) {
          if (target_sp->ResolveLoadAddress(addr, so_addr)) {
            s->PutChar(' ');
            so_addr.Dump(s, exe_scope, Address::DumpStyleResolvedDescription,
                         Address::DumpStyleModuleWithFileAddress);
          } else {
            so_addr.SetOffset(addr);
            so_addr.Dump(s, exe_scope,
                         Address::DumpStyleResolvedPointerDescription);
            if (ProcessSP process_sp = exe_scope->CalculateProcess()) {
              if (ABISP abi_sp = process_sp->GetABI()) {
                addr_t addr_fixed = abi_sp->FixCodeAddress(addr);
                if (target_sp->ResolveLoadAddress(addr_fixed, so_addr)) {
                  s->PutChar(' ');
                  s->Printf("(0x%*.*" PRIx64 ")", (int)(2 * item_byte_size),
                            (int)(2 * item_byte_size), addr_fixed);
                  s->PutChar(' ');
                  so_addr.Dump(s, exe_scope,
                               Address::DumpStyleResolvedDescription,
                               Address::DumpStyleModuleWithFileAddress);
                }
              }
            }
          }
        }
      }
    } break;

    case eFormatHexFloat:
      if (sizeof(float) == item_byte_size) {
        char float_cstr[256];
        llvm::APFloat ap_float(DE.GetFloat(&offset));
        ap_float.convertToHexString(float_cstr, 0, false,
                                    llvm::APFloat::rmNearestTiesToEven);
        s->Printf("%s", float_cstr);
        break;
      } else if (sizeof(double) == item_byte_size) {
        char float_cstr[256];
        llvm::APFloat ap_float(DE.GetDouble(&offset));
        ap_float.convertToHexString(float_cstr, 0, false,
                                    llvm::APFloat::rmNearestTiesToEven);
        s->Printf("%s", float_cstr);
        break;
      } else {
        s->Printf("error: unsupported byte size (%" PRIu64
                  ") for hex float format",
                  (uint64_t)item_byte_size);
        return offset;
      }
      break;

    // please keep the single-item formats below in sync with
    // FormatManager::GetSingleItemFormat if you fail to do so, users will
    // start getting different outputs depending on internal implementation
    // details they should not care about ||
    case eFormatVectorOfChar: //   ||
      s->PutChar('{');        //   \/
      offset =
          DumpDataExtractor(DE, s, offset, eFormatCharArray, 1, item_byte_size,
                            item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt8:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatDecimal, 1, item_byte_size,
                            item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt8:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, 1, item_byte_size,
                                 item_byte_size, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt16:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint16_t),
          item_byte_size / sizeof(uint16_t), item_byte_size / sizeof(uint16_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt16:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint16_t),
                                 item_byte_size / sizeof(uint16_t),
                                 item_byte_size / sizeof(uint16_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt32:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint32_t),
          item_byte_size / sizeof(uint32_t), item_byte_size / sizeof(uint32_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt32:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint32_t),
                                 item_byte_size / sizeof(uint32_t),
                                 item_byte_size / sizeof(uint32_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfSInt64:
      s->PutChar('{');
      offset = DumpDataExtractor(
          DE, s, offset, eFormatDecimal, sizeof(uint64_t),
          item_byte_size / sizeof(uint64_t), item_byte_size / sizeof(uint64_t),
          LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt64:
      s->PutChar('{');
      offset = DumpDataExtractor(DE, s, offset, eFormatHex, sizeof(uint64_t),
                                 item_byte_size / sizeof(uint64_t),
                                 item_byte_size / sizeof(uint64_t),
                                 LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat16:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 2, item_byte_size / 2,
                            item_byte_size / 2, LLDB_INVALID_ADDRESS, 0, 0,
                            exe_scope, false, flt_semantics);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat32:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 4, item_byte_size / 4,
                            item_byte_size / 4, LLDB_INVALID_ADDRESS, 0, 0,
                            exe_scope, false, flt_semantics);
      s->PutChar('}');
      break;

    case eFormatVectorOfFloat64:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatFloat, 8, item_byte_size / 8,
                            item_byte_size / 8, LLDB_INVALID_ADDRESS, 0, 0,
                            exe_scope, false, flt_semantics);
      s->PutChar('}');
      break;

    case eFormatVectorOfUInt128:
      s->PutChar('{');
      offset =
          DumpDataExtractor(DE, s, offset, eFormatHex, 16, item_byte_size / 16,
                            item_byte_size / 16, LLDB_INVALID_ADDRESS, 0, 0);
      s->PutChar('}');
      break;
    }
  }

  // If anything was printed we want to catch the end of the last line.
  // Since we will exit the for loop above before we get a chance to append to
  // it normally.
  if (offset > line_start_offset) {
    if (item_format == eFormatBytesWithASCII) {
      s->Printf("%*s",
                static_cast<int>(
                    (num_per_line - (offset - line_start_offset)) * 3 + 2),
                "");
      DumpDataExtractor(DE, s, line_start_offset, eFormatCharPrintable, 1,
                        offset - line_start_offset, SIZE_MAX,
                        LLDB_INVALID_ADDRESS, 0, 0);
    }

    if (base_addr != LLDB_INVALID_ADDRESS && memory_tag_map) {
      size_t line_len = offset - line_start_offset;
      lldb::addr_t line_base = base_addr + (offset - start_offset - line_len) /
                                               DE.getTargetByteSize();
      printMemoryTags(DE, s, line_base, line_len, memory_tag_map);
    }
  }

  return offset; // Return the offset at which we ended up
}

void lldb_private::DumpHexBytes(Stream *s, const void *src, size_t src_len,
                                uint32_t bytes_per_line,
                                lldb::addr_t base_addr) {
  DataExtractor data(src, src_len, lldb::eByteOrderLittle, 4);
  DumpDataExtractor(data, s,
                    0,                  // Offset into "src"
                    lldb::eFormatBytes, // Dump as hex bytes
                    1,              // Size of each item is 1 for single bytes
                    src_len,        // Number of bytes
                    bytes_per_line, // Num bytes per line
                    base_addr,      // Base address
                    0, 0);          // Bitfield info
}
