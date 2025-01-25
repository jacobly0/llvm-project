//===-- TypeSystemZig.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemZig.h"

#include "ValueObjectZig.h"

#include "Plugins/ExpressionParser/Zig/ZigUserExpression.h"
#include "Plugins/Language/Zig/ZigLexer.h"
#include "Plugins/SymbolFile/DWARF/DWARFASTParserZig.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/StreamString.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <limits>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

LLDB_PLUGIN_DEFINE(TypeSystemZig)

struct TypeSystemZig::ZigDataKeyInfo {
  class Key {
  public:
    Key(ZigType *type, llvm::ArrayRef<uint8_t> data)
        : m_type(type), m_data(data) {}

    explicit Key(const ZigData *value)
        : m_type(value->GetType()), m_data(value->GetData()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_data == that.m_data;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_data);
    }

  private:
    ZigType *m_type;
    llvm::ArrayRef<uint8_t> m_data;
  };

  static inline ZigData *getEmptyKey() {
    return llvm::DenseMapInfo<ZigData *>::getEmptyKey();
  }

  static inline ZigData *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigData *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigData *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigData *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigData *lhs, const ZigData *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigOnlyPossibleValueKeyInfo {
  class Key {
  public:
    Key(ZigType *type) : m_type(type) {}

    explicit Key(const ZigOnlyPossibleValue *value)
        : m_type(value->GetType()) {}

    bool operator==(const Key &that) const { return m_type == that.m_type; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_type);
    }

  private:
    ZigType *m_type;
  };

  static inline ZigOnlyPossibleValue *getEmptyKey() {
    return llvm::DenseMapInfo<ZigOnlyPossibleValue *>::getEmptyKey();
  }

  static inline ZigOnlyPossibleValue *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigOnlyPossibleValue *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigOnlyPossibleValue *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigOnlyPossibleValue *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigOnlyPossibleValue *lhs,
                      const ZigOnlyPossibleValue *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigComptimeIntKeyInfo {
  class Key {
  public:
    Key(const llvm::APInt &value)
        : m_words(value.getRawData(),
                  value.isZero()
                      ? 0
                      : llvm::APInt::getNumWords(value.getSignificantBits())) {}

    explicit Key(const ZigComptimeInt *value) : m_words(value->GetWords()) {}

    bool operator==(const Key &that) const { return m_words == that.m_words; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_words);
    }

  private:
    llvm::ArrayRef<llvm::APInt::WordType> m_words;
  };

  static inline ZigComptimeInt *getEmptyKey() {
    return llvm::DenseMapInfo<ZigComptimeInt *>::getEmptyKey();
  }

  static inline ZigComptimeInt *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigComptimeInt *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigComptimeInt *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigComptimeInt *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigComptimeInt *lhs, const ZigComptimeInt *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigComptimeFloatKeyInfo {
  class Key {
  public:
    Key(const llvm::APFloat &value)
        : m_words(value.bitcastToAPInt().getRawData(),
                  llvm::APInt::getNumWords(128)) {}

    explicit Key(const ZigComptimeFloat *value) : m_words(value->GetWords()) {}

    bool operator==(const Key &that) const { return m_words == that.m_words; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_words);
    }

  private:
    llvm::ArrayRef<llvm::APInt::WordType> m_words;
  };

  static inline ZigComptimeFloat *getEmptyKey() {
    return llvm::DenseMapInfo<ZigComptimeFloat *>::getEmptyKey();
  }

  static inline ZigComptimeFloat *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigComptimeFloat *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigComptimeFloat *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigComptimeFloat *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigComptimeFloat *lhs,
                      const ZigComptimeFloat *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigEnumLiteralKeyInfo {
  class Key {
  public:
    Key(ConstString value) : m_value(value) {}

    explicit Key(const ZigEnumLiteral *value) : m_value(value->GetValue()) {}

    bool operator==(const Key &that) const { return m_value == that.m_value; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_value);
    }

  private:
    ConstString m_value;
  };

  static inline ZigEnumLiteral *getEmptyKey() {
    return llvm::DenseMapInfo<ZigEnumLiteral *>::getEmptyKey();
  }

  static inline ZigEnumLiteral *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigEnumLiteral *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigEnumLiteral *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigEnumLiteral *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigEnumLiteral *lhs, const ZigEnumLiteral *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigIntKeyInfo {
  class Key {
  public:
    Key(ZigIntType *type, const llvm::APInt &value)
        : m_type(type), m_words(value.getRawData(), value.getNumWords()) {}

    explicit Key(const ZigInt *value)
        : m_type(value->GetType()), m_words(value->GetWords()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_words == that.m_words;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_words);
    }

  private:
    ZigIntType *m_type;
    llvm::ArrayRef<llvm::APInt::WordType> m_words;
  };

  static inline ZigInt *getEmptyKey() {
    return llvm::DenseMapInfo<ZigInt *>::getEmptyKey();
  }

  static inline ZigInt *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigInt *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigInt *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigInt *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigInt *lhs, const ZigInt *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigFloatKeyInfo {
  class Key {
  public:
    Key(ZigFloatType *type, const llvm::APFloat &value) : m_type(type) {
      llvm::APInt bits = value.bitcastToAPInt();
      m_words = llvm::ArrayRef<llvm::APInt::WordType>(bits.getRawData(),
                                                      bits.getNumWords());
    }

    explicit Key(const ZigFloat *value)
        : m_type(value->GetType()), m_words(value->GetWords()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_words == that.m_words;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_words);
    }

  private:
    ZigFloatType *m_type;
    llvm::ArrayRef<llvm::APInt::WordType> m_words;
  };

  static inline ZigFloat *getEmptyKey() {
    return llvm::DenseMapInfo<ZigFloat *>::getEmptyKey();
  }

  static inline ZigFloat *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigFloat *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigFloat *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigFloat *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigFloat *lhs, const ZigFloat *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigPointerKeyInfo {
  class Key {
  public:
    Key(ZigPointerType *type, ZigValue *pointee, uint64_t offset)
        : m_type(type), m_pointee(pointee), m_offset(offset) {}

    explicit Key(const ZigPointer *value)
        : m_type(value->GetType()), m_pointee(value->GetPointee()),
          m_offset(value->GetOffset()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_pointee == that.m_pointee &&
             m_offset == that.m_offset;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_pointee, key.m_offset);
    }

  private:
    ZigPointerType *m_type;
    ZigValue *m_pointee;
    uint64_t m_offset;
  };

  static inline ZigPointer *getEmptyKey() {
    return llvm::DenseMapInfo<ZigPointer *>::getEmptyKey();
  }

  static inline ZigPointer *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigPointer *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigPointer *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigPointer *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigPointer *lhs, const ZigPointer *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigSliceKeyInfo {
  class Key {
  public:
    Key(ZigPointerType *type, ZigValue *pointee, uint64_t offset, uint64_t len)
        : m_type(type), m_pointee(pointee), m_offset(offset), m_len(len) {}

    explicit Key(const ZigSlice *value)
        : m_type(value->GetType()), m_pointee(value->GetPointee()),
          m_offset(value->GetOffset()), m_len(value->GetLength()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_pointee == that.m_pointee &&
             m_offset == that.m_offset && m_len == that.m_len;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_pointee, key.m_offset,
                                key.m_len);
    }

  private:
    ZigPointerType *m_type;
    ZigValue *m_pointee;
    uint64_t m_offset;
    uint64_t m_len;
  };

  static inline ZigSlice *getEmptyKey() {
    return llvm::DenseMapInfo<ZigSlice *>::getEmptyKey();
  }

  static inline ZigSlice *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigSlice *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigSlice *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigSlice *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigSlice *lhs, const ZigSlice *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigTagKeyInfo {
  class Key {
  public:
    Key(ZigTagType *type, ZigInt *value) : m_type(type), m_value(value) {}

    explicit Key(const ZigTag *value)
        : m_type(value->GetType()), m_value(value->GetValue()) {}

    bool operator==(const Key &that) const {
      return m_type == that.m_type && m_value == that.m_value;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_type, key.m_value);
    }

  private:
    ZigTagType *m_type;
    ZigInt *m_value;
  };

  static inline ZigTag *getEmptyKey() {
    return llvm::DenseMapInfo<ZigTag *>::getEmptyKey();
  }

  static inline ZigTag *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigTag *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigTag *value) {
    return hash_value(Key(value));
  }

  static bool isEqual(const Key &lhs, const ZigTag *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigTag *lhs, const ZigTag *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigNamedIntTypeKeyInfo {
  class Key {
  public:
    Key(ConstString name) : m_name(name) {}

    explicit Key(const ZigIntType *type) : m_name(type->GetName()) {}

    bool operator==(const Key &that) const { return m_name == that.m_name; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_name);
    }

  private:
    ConstString m_name;
  };

  static inline ZigIntType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigIntType *>::getEmptyKey();
  }

  static inline ZigIntType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigIntType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigIntType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigIntType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigIntType *lhs, const ZigIntType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigIntTypeKeyInfo {
  class Key {
  public:
    Key(bool is_signed, uint16_t bit_size)
        : m_is_signed(is_signed), m_bit_size(bit_size) {}

    explicit Key(const ZigIntType *type)
        : m_is_signed(type->IsSigned()), m_bit_size(type->GetBitSize()) {}

    bool operator==(const Key &that) const {
      return m_is_signed == that.m_is_signed && m_bit_size == that.m_bit_size;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_is_signed, key.m_bit_size);
    }

  private:
    bool m_is_signed;
    uint16_t m_bit_size;
  };

  static inline ZigIntType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigIntType *>::getEmptyKey();
  }

  static inline ZigIntType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigIntType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigIntType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigIntType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigIntType *lhs, const ZigIntType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigOptionalTypeKeyInfo {
  class Key {
  public:
    Key(ZigType *child_type) : m_child_type(child_type) {}

    bool operator==(const Key &that) const {
      return m_child_type == that.m_child_type;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_child_type);
    }

  private:
    ZigType *m_child_type;
  };

  static inline ZigOptionalType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigOptionalType *>::getEmptyKey();
  }

  static inline ZigOptionalType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigOptionalType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigOptionalType *type) {
    return hash_value(Key(type->GetChildType()));
  }

  static bool isEqual(const Key &lhs, const ZigOptionalType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() &&
           lhs == rhs->GetChildType();
  }

  static bool isEqual(const ZigOptionalType *lhs, const ZigOptionalType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigPointerTypeKeyInfo {
  class Key {
  public:
    Key(ZigPointerType::Size size, ZigValue *sentinel, bool is_allowzero,
        llvm::Align pointer_align, ZigPointerType::AddressSpace addrspace,
        bool is_const, bool is_volatile, ZigType *child_type)
        : m_size(size), m_sentinel(sentinel), m_is_allowzero(is_allowzero),
          m_pointer_align(pointer_align), m_addrspace(addrspace),
          m_is_const(is_const), m_is_volatile(is_volatile),
          m_child_type(child_type) {}

    Key(const ZigPointerType *type)
        : m_size(type->GetSize()), m_sentinel(type->GetSentinel()),
          m_is_allowzero(type->IsAllowZero()),
          m_pointer_align(type->GetPointerAlign()),
          m_addrspace(type->GetAddressSpace()), m_is_const(type->IsConst()),
          m_is_volatile(type->IsVolatile()),
          m_child_type(type->GetChildType()) {}

    bool operator==(const Key &that) const {
      return m_size == that.m_size && m_sentinel == that.m_sentinel &&
             m_is_allowzero == that.m_is_allowzero &&
             m_pointer_align == that.m_pointer_align &&
             m_addrspace == that.m_addrspace && m_is_const == that.m_is_const &&
             m_is_volatile == that.m_is_volatile &&
             m_child_type == that.m_child_type;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_size, key.m_sentinel, key.m_is_allowzero,
                                Log2(key.m_pointer_align), key.m_addrspace,
                                key.m_is_const, key.m_is_volatile,
                                key.m_child_type);
    }

  private:
    ZigPointerType::Size m_size;
    ZigValue *m_sentinel;
    bool m_is_allowzero;
    llvm::Align m_pointer_align;
    ZigPointerType::AddressSpace m_addrspace;
    bool m_is_const;
    bool m_is_volatile;
    ZigType *m_child_type;
  };

  static inline ZigPointerType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigPointerType *>::getEmptyKey();
  }

  static inline ZigPointerType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigPointerType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigPointerType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigPointerType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigPointerType *lhs, const ZigPointerType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigErrorUnionTypeKeyInfo {
  class Key {
  public:
    Key(ZigErrorSetType *error_set, ZigType *payload)
        : m_error_set(error_set), m_payload(payload) {}

    explicit Key(const ZigErrorUnionType *type)
        : m_error_set(type->GetErrorSet()), m_payload(type->GetPayload()) {}

    bool operator==(const Key &that) const {
      return m_error_set == that.m_error_set && m_payload == that.m_payload;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_error_set, key.m_payload);
    }

  private:
    ZigErrorSetType *m_error_set;
    ZigType *m_payload;
  };

  static inline ZigErrorUnionType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigErrorUnionType *>::getEmptyKey();
  }

  static inline ZigErrorUnionType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigErrorUnionType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigErrorUnionType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigErrorUnionType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigErrorUnionType *lhs,
                      const ZigErrorUnionType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigArrayTypeKeyInfo {
  class Key {
  public:
    Key(uint64_t len, ZigValue *sentinel, ZigType *child_type)
        : m_len(len), m_sentinel(sentinel), m_child_type(child_type) {}

    explicit Key(const ZigArrayType *type)
        : m_len(type->GetLength()), m_sentinel(type->GetSentinel()),
          m_child_type(type->GetChildType()) {}

    bool operator==(const Key &that) const {
      return m_len == that.m_len && m_sentinel == that.m_sentinel &&
             m_child_type == that.m_child_type;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_len, key.m_sentinel, key.m_child_type);
    }

  private:
    uint64_t m_len;
    ZigValue *m_sentinel;
    ZigType *m_child_type;
  };

  static inline ZigArrayType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigArrayType *>::getEmptyKey();
  }

  static inline ZigArrayType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigArrayType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigArrayType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigArrayType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigArrayType *lhs, const ZigArrayType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigVectorTypeKeyInfo {
  class Key {
  public:
    Key(uint32_t len, ZigType *child_type)
        : m_len(len), m_child_type(child_type) {}

    explicit Key(const ZigVectorType *type)
        : m_len(type->GetLength()), m_child_type(type->GetChildType()) {}

    bool operator==(const Key &that) const {
      return m_len == that.m_len && m_child_type == that.m_child_type;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_len, key.m_child_type);
    }

  private:
    uint32_t m_len;
    ZigType *m_child_type;
  };

  static inline ZigVectorType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigVectorType *>::getEmptyKey();
  }

  static inline ZigVectorType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigVectorType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigVectorType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigVectorType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigVectorType *lhs, const ZigVectorType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigFunctionTypeKeyInfo {
  class Key {
  public:
    Key(llvm::ArrayRef<ZigType *> param_types, bool is_var_args,
        ZigType *ret_type)
        : m_param_types(param_types), m_is_var_args(is_var_args),
          m_ret_type(ret_type) {}

    Key(const ZigFunctionType *type)
        : m_param_types(type->GetParamTypes()),
          m_is_var_args(type->IsVarArgs()), m_ret_type(type->GetReturnType()) {}

    bool operator==(const Key &that) const {
      return m_param_types == that.m_param_types &&
             m_is_var_args == that.m_is_var_args &&
             m_ret_type == that.m_ret_type;
    }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_combine(key.m_param_types, key.m_is_var_args,
                                key.m_ret_type);
    }

  private:
    llvm::ArrayRef<ZigType *> m_param_types;
    bool m_is_var_args;
    ZigType *m_ret_type;
  };

  static inline ZigFunctionType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigFunctionType *>::getEmptyKey();
  }

  static inline ZigFunctionType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigFunctionType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigFunctionType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigFunctionType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigFunctionType *lhs, const ZigFunctionType *rhs) {
    return lhs == rhs;
  }
};

struct TypeSystemZig::ZigTupleTypeKeyInfo {
  class Key {
  public:
    Key(llvm::ArrayRef<ZigTupleField> fields) : m_fields(fields) {}

    Key(const ZigTupleType *type) : m_fields(type->GetFields()) {}

    bool operator==(const Key &that) const { return m_fields == that.m_fields; }

    bool operator!=(const Key &that) const { return !(*this == that); }

    friend llvm::hash_code hash_value(const Key &key) {
      return llvm::hash_value(key.m_fields);
    }

  private:
    llvm::ArrayRef<ZigTupleField> m_fields;
  };

  static inline ZigTupleType *getEmptyKey() {
    return llvm::DenseMapInfo<ZigTupleType *>::getEmptyKey();
  }

  static inline ZigTupleType *getTombstoneKey() {
    return llvm::DenseMapInfo<ZigTupleType *>::getTombstoneKey();
  }

  static unsigned getHashValue(const Key &key) { return hash_value(key); }

  static unsigned getHashValue(const ZigTupleType *type) {
    return hash_value(Key(type));
  }

  static bool isEqual(const Key &lhs, const ZigTupleType *rhs) {
    return rhs != getEmptyKey() && rhs != getTombstoneKey() && lhs == Key(rhs);
  }

  static bool isEqual(const ZigTupleType *lhs, const ZigTupleType *rhs) {
    return lhs == rhs;
  }
};

static inline bool TypeSystemZigSupportsLanguage(LanguageType language) {
  return language == eLanguageTypeZig;
}

static llvm::Align MaxIntAlignment(ArchSpec arch) {
  switch (arch.GetMachine()) {
  case llvm::Triple::ArchType::UnknownArch:
  case llvm::Triple::ArchType::r600:
  case llvm::Triple::ArchType::tce:
  case llvm::Triple::ArchType::tcele:
  case llvm::Triple::ArchType::amdil:
  case llvm::Triple::ArchType::amdil64:
  case llvm::Triple::ArchType::hsail:
  case llvm::Triple::ArchType::hsail64:
  case llvm::Triple::ArchType::shave:
  case llvm::Triple::ArchType::renderscript32:
  case llvm::Triple::ArchType::renderscript64:
    llvm_unreachable("unsupported arch");
  case llvm::Triple::ArchType::avr:
    return llvm::Align::Constant<1>();
  case llvm::Triple::ArchType::msp430:
    return llvm::Align::Constant<2>();
  case llvm::Triple::ArchType::xcore:
    return llvm::Align::Constant<4>();
  case llvm::Triple::ArchType::arm:
  case llvm::Triple::ArchType::armeb:
  case llvm::Triple::ArchType::thumb:
  case llvm::Triple::ArchType::thumbeb:
  case llvm::Triple::ArchType::hexagon:
  case llvm::Triple::ArchType::mips:
  case llvm::Triple::ArchType::mipsel:
  case llvm::Triple::ArchType::ppc:
  case llvm::Triple::ArchType::ppcle:
  case llvm::Triple::ArchType::amdgcn:
  case llvm::Triple::ArchType::riscv32:
  case llvm::Triple::ArchType::sparc:
  case llvm::Triple::ArchType::sparcel:
  case llvm::Triple::ArchType::systemz:
  case llvm::Triple::ArchType::lanai:
  case llvm::Triple::ArchType::wasm32:
  case llvm::Triple::ArchType::wasm64:
  case llvm::Triple::ArchType::ppc64:
  case llvm::Triple::ArchType::ppc64le:
  case llvm::Triple::ArchType::mips64:
  case llvm::Triple::ArchType::mips64el:
  case llvm::Triple::ArchType::sparcv9:
    return llvm::Align::Constant<8>();
  case llvm::Triple::ArchType::x86_64:
  case llvm::Triple::ArchType::x86:
  case llvm::Triple::ArchType::aarch64:
  case llvm::Triple::ArchType::aarch64_be:
  case llvm::Triple::ArchType::aarch64_32:
  case llvm::Triple::ArchType::riscv64:
  case llvm::Triple::ArchType::bpfel:
  case llvm::Triple::ArchType::bpfeb:
  case llvm::Triple::ArchType::nvptx:
  case llvm::Triple::ArchType::nvptx64:
  case llvm::Triple::ArchType::csky:
  case llvm::Triple::ArchType::arc:
  case llvm::Triple::ArchType::m68k:
  case llvm::Triple::ArchType::spir:
  case llvm::Triple::ArchType::kalimba:
  case llvm::Triple::ArchType::spirv:
  case llvm::Triple::ArchType::spirv32:
  case llvm::Triple::ArchType::spir64:
  case llvm::Triple::ArchType::ve:
  case llvm::Triple::ArchType::spirv64:
  case llvm::Triple::ArchType::dxil:
  case llvm::Triple::ArchType::loongarch32:
  case llvm::Triple::ArchType::loongarch64:
  case llvm::Triple::ArchType::xtensa:
    return llvm::Align::Constant<16>();
  }
}
static llvm::Align IntAbiAlignment(unsigned bit_size, ArchSpec arch) {
  return commonAlignment(MaxIntAlignment(arch),
                         llvm::PowerOf2Ceil(llvm::divideCeil(bit_size, 8)));
}
static unsigned IntAbiSize(unsigned bit_size, ArchSpec arch) {
  return alignTo(llvm::divideCeil(bit_size, 8),
                 IntAbiAlignment(bit_size, arch));
}
static llvm::Align FloatAbiAlignment(unsigned bit_size, ArchSpec arch) {
  // TODO: implement
  return IntAbiAlignment(bit_size, arch);
}

char TypeSystemZig::ID;

TypeSystemZig::TypeSystemZig(llvm::StringRef name, ArchSpec arch)
    : m_arch(arch), m_display_name(name) {}

// Destructor
TypeSystemZig::~TypeSystemZig() { Finalize(); }

TypeSystemSP TypeSystemZig::CreateInstance(LanguageType language,
                                           Module *module, Target *target) {
  if (!TypeSystemZigSupportsLanguage(language))
    return TypeSystemSP();

  if (module) {
    std::string name =
        "ASTContext for '" + module->GetFileSpec().GetPath() + "'";
    return std::make_shared<TypeSystemZig>(name, module->GetArchitecture());
  } else if (target && target->IsValid())
    return std::make_shared<ScratchTypeSystemZig>(*target);
  return TypeSystemSP();
}

void TypeSystemZig::Initialize() {
  LanguageSet languages;
  languages.Insert(eLanguageTypeZig);
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "zig type system plug-in", CreateInstance,
                                languages, languages);
}

void TypeSystemZig::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

const clang::TargetInfo *TypeSystemZig::GetTargetInfo() {
  if (!m_target_info_up) {
    clang::DiagnosticsEngine diagnostics_engine(new clang::DiagnosticIDs,
                                                new clang::DiagnosticOptions);
    class NullDiagnosticConsumer : public clang::DiagnosticConsumer {
    public:
      NullDiagnosticConsumer() { m_log = GetLog(LLDBLog::Expressions); }

      void HandleDiagnostic(clang::DiagnosticsEngine::Level DiagLevel,
                            const clang::Diagnostic &info) override {
        if (m_log) {
          llvm::SmallVector<char, 32> diag_str(10);
          info.FormatDiagnostic(diag_str);
          diag_str.push_back('\0');
          LLDB_LOGF(m_log, "Compiler diagnostic: %s\n", diag_str.data());
        }
      }

      DiagnosticConsumer *clone(clang::DiagnosticsEngine &Diags) const {
        return new NullDiagnosticConsumer();
      }

    private:
      Log *m_log;
    } diagnostic_consumer;
    diagnostics_engine.setClient(&diagnostic_consumer, false);
    auto target_options = std::make_shared<clang::TargetOptions>();
    target_options->Triple = m_arch.GetTriple().getTriple();
    m_target_info_up.reset(clang::TargetInfo::CreateTargetInfo(
        diagnostics_engine, target_options));
  }
  return m_target_info_up.get();
}

ZigModule *TypeSystemZig::GetModule(ConstString name) {
  ZigModule *module = m_allocator.Allocate<ZigModule>();
  new (module) ZigModule(this, name);
  return module;
}

ZigConstant *TypeSystemZig::GetConstant(void *opaque_decl_ctx, ConstString name,
                                        ZigValue *value) {
  if (!opaque_decl_ctx || !value)
    return nullptr;
  ZigConstant *constant = m_allocator.Allocate<ZigConstant>();
  new (constant) ZigConstant(this, UnwrapScope(opaque_decl_ctx), name, value);
  return constant;
}
ZigAlias *TypeSystemZig::GetAlias(void *opaque_decl_ctx, ConstString name,
                                  ZigType *value) {
  if (!opaque_decl_ctx || !value)
    return nullptr;
  ZigAlias *alias = m_allocator.Allocate<ZigAlias>();
  new (alias) ZigAlias(this, UnwrapScope(opaque_decl_ctx), name, value);
  return alias;
}
ZigVariable *TypeSystemZig::GetVariable(void *opaque_decl_ctx, ConstString name,
                                        opaque_compiler_type_t type) {
  if (!opaque_decl_ctx || !type)
    return nullptr;
  ZigVariable *variable = m_allocator.Allocate<ZigVariable>();
  new (variable)
      ZigVariable(this, UnwrapScope(opaque_decl_ctx), name, UnwrapType(type));
  return variable;
}
ZigFunction *TypeSystemZig::GetFunction(void *opaque_decl_ctx, ConstString name,
                                        opaque_compiler_type_t type) {
  ZigFunctionType *zig_type =
      llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type));
  if (!opaque_decl_ctx || !zig_type)
    return nullptr;
  ZigFunction *function = m_allocator.Allocate<ZigFunction>();
  new (function)
      ZigFunction(this, UnwrapScope(opaque_decl_ctx), name, zig_type);
  return function;
}
ZigBlock *TypeSystemZig::GetBlock(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return nullptr;
  ZigBlock *block = m_allocator.Allocate<ZigBlock>();
  new (block) ZigBlock(this, UnwrapScope(opaque_decl_ctx));
  return block;
}

ZigValue *TypeSystemZig::GetData(ZigType *type, llvm::ArrayRef<uint8_t> data) {
  if (type->GetByteSize() != data.size())
    return nullptr;
  if (data.size() == 0)
    return GetOnlyPossibleValue(type);
  if (llvm::isa<ZigBoolType>(type)) {
    assert(data.size() == 1 && "wrong data size");
    assert(data[0] <= 1 && "corrupt data");
    return GetBool(data[0]);
  }
  if (ZigIntType *int_type = llvm::dyn_cast<ZigIntType>(type))
    return GetInt(int_type, LoadTargetInt(int_type, data));
  if (ZigFloatType *float_type = llvm::dyn_cast<ZigFloatType>(type))
    return GetFloat(float_type, llvm::APFloat(float_type->GetSemantics(),
                                              LoadTargetInt(float_type, data)));
  if (ZigTagType *tag_type = llvm::dyn_cast<ZigTagType>(type))
    return GetTag(tag_type, LoadTargetInt(tag_type->GetBackingType(), data));
  ZigDataKeyInfo::Key key(type, data);
  ZigData *&entry = *m_data.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigData *>(m_allocator.Allocate(
        ZigData::totalSizeToAlloc<uint8_t>(data.size()), alignof(ZigData)));
    new (entry) ZigData(this, type, data);
  }
  return entry;
}
ZigOnlyPossibleValue *TypeSystemZig::GetOnlyPossibleValue(ZigType *type) {
  if (!type)
    return nullptr;
  assert(!type->GetByteSize() && "type has runtime bits");
  assert(type->HasOnePossibleValue() && "type has multiple possible values");
  ZigOnlyPossibleValueKeyInfo::Key key(type);
  ZigOnlyPossibleValue *&entry =
      *m_only_possible_values.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigOnlyPossibleValue>();
    new (entry) ZigOnlyPossibleValue(this, type);
  }
  assert(entry->GetKind() == ZigValue::Kind::OnlyPossibleValue);
  return entry;
}
ZigOnlyPossibleValue *TypeSystemZig::GetVoid() {
  return GetOnlyPossibleValue(UnwrapType(GetVoidType()));
}
ZigComptimeInt *TypeSystemZig::GetComptimeInt(const llvm::APSInt &value) {
  ZigComptimeIntKeyInfo::Key key(value);
  ZigComptimeInt *&entry = *m_comptime_ints.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigComptimeInt *>(m_allocator.Allocate(
        ZigComptimeInt::totalSizeToAlloc<llvm::APInt::WordType>(
            value.getNumWords()),
        alignof(ZigComptimeInt)));
    new (entry) ZigComptimeInt(this, value);
  }
  return entry;
}
ZigComptimeFloat *TypeSystemZig::GetComptimeFloat(const llvm::APFloat &value) {
  ZigComptimeFloatKeyInfo::Key key(value);
  ZigComptimeFloat *&entry = *m_comptime_floats.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigComptimeFloat>();
    new (entry) ZigComptimeFloat(this, value);
  }
  return entry;
}
ZigOnlyPossibleValue *TypeSystemZig::GetUndefined() {
  return GetOnlyPossibleValue(UnwrapType(GetUndefinedType()));
}
ZigOnlyPossibleValue *TypeSystemZig::GetNull() {
  return GetOnlyPossibleValue(UnwrapType(GetNullType()));
}
ZigEnumLiteral *TypeSystemZig::GetEnumLiteral(ConstString value) {
  ZigEnumLiteralKeyInfo::Key key(value);
  ZigEnumLiteral *&entry = *m_enum_literals.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigEnumLiteral>();
    new (entry) ZigEnumLiteral(this, value);
  }
  return entry;
}
ZigBool *TypeSystemZig::GetBool(bool value) {
  ZigBool *&entry = value ? m_true : m_false;
  if (!entry) {
    entry = m_allocator.Allocate<ZigBool>();
    new (entry) ZigBool(this, value);
  }
  return entry;
}
ZigInt *TypeSystemZig::GetInt(ZigIntType *type, const llvm::APInt &value) {
  if (!type)
    return nullptr;
  ZigIntKeyInfo::Key key(type, value);
  ZigInt *&entry = *m_ints.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigInt *>(m_allocator.Allocate(
        ZigInt::totalSizeToAlloc<llvm::APInt::WordType>(value.getNumWords()),
        alignof(ZigInt)));
    new (entry) ZigInt(this, type, value);
  }
  return entry;
}
ZigFloat *TypeSystemZig::GetFloat(ZigFloatType *type,
                                  const llvm::APFloat &value) {
  if (!type)
    return nullptr;
  ZigFloatKeyInfo::Key key(type, value);
  ZigFloat *&entry = *m_floats.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigFloat *>(
        m_allocator.Allocate(ZigFloat::totalSizeToAlloc<llvm::APInt::WordType>(
                                 llvm::APInt::getNumWords(type->GetBitSize())),
                             alignof(ZigFloat)));
    new (entry) ZigFloat(this, type, value);
  }
  return entry;
}
ZigPointer *TypeSystemZig::GetPointer(ZigPointerType *type, ZigValue *pointee,
                                      uint64_t offset) {
  if (!type)
    return nullptr;
  ZigPointerKeyInfo::Key key(type, pointee, offset);
  ZigPointer *&entry = *m_pointers.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigPointer>();
    new (entry) ZigPointer(this, type, pointee, offset);
  }
  return entry;
}
ZigSlice *TypeSystemZig::GetSlice(ZigPointerType *type, ZigValue *pointee,
                                  uint64_t offset, uint64_t len) {
  if (!type || !pointee)
    return nullptr;
  ZigSliceKeyInfo::Key key(type, pointee, offset, len);
  ZigSlice *&entry = *m_slices.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigSlice>();
    new (entry) ZigSlice(this, type, pointee, offset, len);
  }
  return entry;
}
ZigTag *TypeSystemZig::GetTag(ZigTagType *type, const llvm::APInt &value) {
  if (!type)
    return nullptr;
  ZigInt *zig_value = GetInt(type->GetBackingType(), value);
  ZigTagKeyInfo::Key key(type, zig_value);
  ZigTag *&entry = *m_tags.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigTag>();
    new (entry) ZigTag(this, type, zig_value);
  }
  return entry;
}

CompilerType TypeSystemZig::GetTypeType() {
  if (!m_type_type) {
    m_type_type = m_allocator.Allocate<ZigTypeType>();
    new (m_type_type) ZigTypeType(this);
  }
  return WrapType(m_type_type);
}
CompilerType TypeSystemZig::GetVoidType() {
  if (!m_void_type) {
    m_void_type = m_allocator.Allocate<ZigVoidType>();
    new (m_void_type) ZigVoidType(this);
  }
  return WrapType(m_void_type);
}
CompilerType TypeSystemZig::GetNoReturnType() {
  if (!m_no_return_type) {
    m_no_return_type = m_allocator.Allocate<ZigNoReturnType>();
    new (m_no_return_type) ZigNoReturnType(this);
  }
  return WrapType(m_no_return_type);
}
CompilerType TypeSystemZig::GetComptimeIntType() {
  if (!m_comptime_int_type) {
    m_comptime_int_type = m_allocator.Allocate<ZigComptimeIntType>();
    new (m_comptime_int_type) ZigComptimeIntType(this);
  }
  return WrapType(m_comptime_int_type);
}
CompilerType TypeSystemZig::GetComptimeFloatType() {
  if (!m_comptime_float_type) {
    m_comptime_float_type = m_allocator.Allocate<ZigComptimeFloatType>();
    new (m_comptime_float_type) ZigComptimeFloatType(this);
  }
  return WrapType(m_comptime_float_type);
}
CompilerType TypeSystemZig::GetUndefinedType() {
  if (!m_undefined_type) {
    m_undefined_type = m_allocator.Allocate<ZigUndefinedType>();
    new (m_undefined_type) ZigUndefinedType(this);
  }
  return WrapType(m_undefined_type);
}
CompilerType TypeSystemZig::GetNullType() {
  if (!m_null_type) {
    m_null_type = m_allocator.Allocate<ZigNullType>();
    new (m_null_type) ZigNullType(this);
  }
  return WrapType(m_null_type);
}
CompilerType TypeSystemZig::GetAnyOpaqueType() {
  if (!m_any_opaque_type) {
    m_any_opaque_type = m_allocator.Allocate<ZigAnyOpaqueType>();
    new (m_any_opaque_type) ZigAnyOpaqueType(this);
  }
  return WrapType(m_any_opaque_type);
}
CompilerType TypeSystemZig::GetEnumLiteralType() {
  if (!m_enum_literal_type) {
    m_enum_literal_type = m_allocator.Allocate<ZigEnumLiteralType>();
    new (m_enum_literal_type) ZigEnumLiteralType(this);
  }
  return WrapType(m_enum_literal_type);
}
CompilerType TypeSystemZig::GetAnyType() {
  if (!m_any_type) {
    m_any_type = m_allocator.Allocate<ZigAnyType>();
    new (m_any_type) ZigAnyType(this);
  }
  return WrapType(m_any_type);
}
CompilerType TypeSystemZig::GetSizeType(bool is_signed) {
  uint16_t bit_size = GetPointerByteSize() * 8;
  return GetIntType(ConstString(is_signed ? "isize" : "usize"), is_signed,
                    bit_size, IntAbiSize(bit_size, m_arch),
                    IntAbiAlignment(bit_size, m_arch));
}

CompilerType TypeSystemZig::GetBoolType() {
  if (!m_bool_type) {
    m_bool_type = m_allocator.Allocate<ZigBoolType>();
    new (m_bool_type) ZigBoolType(this);
  }
  return WrapType(m_bool_type);
}
CompilerType TypeSystemZig::GetIntType(ConstString name, bool is_signed,
                                       uint16_t bit_size, uint16_t byte_size,
                                       llvm::MaybeAlign align) {
  bool is_named =
      llvm::StringSwitch<bool>(name)
          .Case("c_char", is_signed == m_arch.CharIsSignedByDefault())
          .Cases("isize", "c_short", "c_int", "c_long", "c_longlong", is_signed)
          .Cases("usize", "c_ushort", "c_uint", "c_ulong", "c_ulonglong",
                 !is_signed)
          .Default(false);
  ZigIntType *&entry =
      is_named ? *m_named_int_types.insert_as(nullptr, name).first
               : *m_int_types
                      .insert_as(nullptr,
                                 ZigIntTypeKeyInfo::Key(is_signed, bit_size))
                      .first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigIntType>();
    new (entry) ZigIntType(this, is_named ? name : ConstString(), is_signed,
                           bit_size, byte_size,
                           align ? *align : IntAbiAlignment(bit_size, m_arch));
  }
  return WrapType(entry);
}
CompilerType TypeSystemZig::GetFloatType(ConstString name, uint16_t bit_size,
                                         uint16_t byte_size,
                                         llvm::MaybeAlign align) {
  ZigFloatType **entry;
  if (name == "c_longdouble") {
    entry = &m_c_longdouble_type;
  } else {
    name = ConstString();
    switch (bit_size) {
    case 16:
      entry = &m_f16_type;
      break;
    case 32:
      entry = &m_f32_type;
      break;
    case 64:
      entry = &m_f64_type;
      break;
    case 80:
      entry = &m_f80_type;
      break;
    case 128:
      entry = &m_f128_type;
      break;
    }
  }
  if (!*entry) {
    *entry = m_allocator.Allocate<ZigFloatType>();
    new (*entry)
        ZigFloatType(this, name, bit_size, byte_size,
                     align ? *align : FloatAbiAlignment(bit_size, m_arch));
  }
  return WrapType(*entry);
}

CompilerType TypeSystemZig::GetOptionalType(opaque_compiler_type_t child) {
  if (!child)
    return WrapType();
  ZigType *child_type = UnwrapType(child);
  ZigOptionalType *&entry =
      *m_optional_types.insert_as(nullptr, child_type).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigOptionalType>();
    new (entry) ZigOptionalType(this, child_type);
  }
  return WrapType(entry);
}

CompilerType TypeSystemZig::GetPointerType(
    ZigPointerType::Size size, ZigValue *sentinel, bool is_allowzero,
    llvm::MaybeAlign pointer_align, ZigPointerType::AddressSpace addrspace,
    bool is_const, bool is_volatile, opaque_compiler_type_t child) {
  if (!child)
    return WrapType();
  ZigType *child_type = UnwrapType(child);
  llvm::Align final_pointer_align(
      pointer_align.value_or(child_type->GetAlign()));
  ZigPointerTypeKeyInfo::Key key(size, sentinel, is_allowzero,
                                 final_pointer_align, addrspace, is_const,
                                 is_volatile, child_type);
  ZigPointerType *&entry = *m_pointer_types.insert_as(nullptr, key).first;
  if (!entry) {
    uint64_t byte_size;
    llvm::Align byte_align;
    if (const clang::TargetInfo *target_info = GetTargetInfo()) {
      clang::LangAS lang_as;
      switch (addrspace) {
      case ZigPointerType::AddressSpace::Generic:
        lang_as = clang::LangAS::Default;
        break;
      case ZigPointerType::AddressSpace::Gs:
        lang_as = clang::getLangASFromTargetAS(256);
        break;
      case ZigPointerType::AddressSpace::Fs:
        lang_as = clang::getLangASFromTargetAS(257);
        break;
      case ZigPointerType::AddressSpace::Ss:
        lang_as = clang::getLangASFromTargetAS(258);
        break;
      case ZigPointerType::AddressSpace::Global:
        lang_as = clang::getLangASFromTargetAS(2);
        break;
      case ZigPointerType::AddressSpace::Constant:
        lang_as = clang::getLangASFromTargetAS(2);
        break;
      case ZigPointerType::AddressSpace::Param:
        lang_as = clang::getLangASFromTargetAS(2);
        break;
      case ZigPointerType::AddressSpace::Shared:
        lang_as = clang::getLangASFromTargetAS(1);
        break;
      case ZigPointerType::AddressSpace::Local:
        lang_as = clang::getLangASFromTargetAS(0);
        break;
      case ZigPointerType::AddressSpace::Input:
        lang_as = clang::getLangASFromTargetAS(7);
        break;
      case ZigPointerType::AddressSpace::Output:
        lang_as = clang::getLangASFromTargetAS(7);
        break;
      case ZigPointerType::AddressSpace::Uniform:
        lang_as = clang::getLangASFromTargetAS(1);
        break;
      case ZigPointerType::AddressSpace::Flash:
        lang_as = clang::getLangASFromTargetAS(1);
        break;
      case ZigPointerType::AddressSpace::Flash1:
        lang_as = clang::getLangASFromTargetAS(2);
        break;
      case ZigPointerType::AddressSpace::Flash2:
        lang_as = clang::getLangASFromTargetAS(3);
        break;
      case ZigPointerType::AddressSpace::Flash3:
        lang_as = clang::getLangASFromTargetAS(4);
        break;
      case ZigPointerType::AddressSpace::Flash4:
        lang_as = clang::getLangASFromTargetAS(5);
        break;
      case ZigPointerType::AddressSpace::Flash5:
        lang_as = clang::getLangASFromTargetAS(6);
        break;
      }
      byte_size = llvm::divideCeil(target_info->getPointerWidth(lang_as), 8);
      byte_align = llvm::Align(
          llvm::divideCeil(target_info->getPointerAlign(lang_as), 8));
    } else {
      byte_size = GetPointerByteSize();
      byte_align = commonAlignment(llvm::Align(), byte_size);
    }
    switch (size) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      break;
    case ZigPointerType::Size::Slice:
      byte_size *= 2;
      break;
    }
    entry = m_allocator.Allocate<ZigPointerType>();
    new (entry) ZigPointerType(this, size, sentinel, is_allowzero,
                               pointer_align, addrspace, is_const, is_volatile,
                               child_type, byte_size, byte_align);
  }
  return WrapType(entry);
}

CompilerType
TypeSystemZig::GetErrorSetType(ConstString name,
                               opaque_compiler_type_t backing_int_type,
                               llvm::ArrayRef<ZigTagField> errors) {
  ZigIntType *zig_backing_int_type =
      llvm::dyn_cast_if_present<ZigIntType>(UnwrapType(backing_int_type));
  if (!zig_backing_int_type)
    return WrapType();
  ZigErrorSetType *zig_type =
      static_cast<ZigErrorSetType *>(m_allocator.Allocate(
          ZigErrorSetType::totalSizeToAlloc<ZigTagField>(errors.size()),
          alignof(ZigErrorSetType)));
  new (zig_type) ZigErrorSetType(this, name, zig_backing_int_type, errors);
  return WrapType(zig_type);
}
CompilerType
TypeSystemZig::GetErrorUnionType(opaque_compiler_type_t error_set_type,
                                 opaque_compiler_type_t payload_type) {
  ZigErrorSetType *zig_error_set_type =
      llvm::dyn_cast_if_present<ZigErrorSetType>(UnwrapType(error_set_type));
  ZigType *zig_payload_type = UnwrapType(payload_type);
  if (!zig_error_set_type || !zig_payload_type)
    return WrapType();
  ZigErrorUnionTypeKeyInfo::Key key(zig_error_set_type, zig_payload_type);
  ZigErrorUnionType *&entry =
      *m_error_union_types.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigErrorUnionType>();
    new (entry) ZigErrorUnionType(this, zig_error_set_type, zig_payload_type);
  }
  return WrapType(entry);
}

CompilerType TypeSystemZig::GetArrayType(uint64_t size, ZigValue *sentinel,
                                         opaque_compiler_type_t type) {
  if (!type)
    return WrapType();
  ZigType *child_type = UnwrapType(type);
  ZigArrayTypeKeyInfo::Key key(size, sentinel, child_type);
  ZigArrayType *&entry = *m_array_types.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigArrayType>();
    new (entry) ZigArrayType(this, size, sentinel, child_type);
  }
  return WrapType(entry);
}
CompilerType TypeSystemZig::GetVectorType(uint32_t size,
                                          opaque_compiler_type_t type) {
  if (!type)
    return WrapType();
  ZigType *child_type = UnwrapType(type);
  ZigVectorTypeKeyInfo::Key key(size, child_type);
  ZigVectorType *&entry = *m_vector_types.insert_as(nullptr, key).first;
  if (!entry) {
    entry = m_allocator.Allocate<ZigVectorType>();
    new (entry) ZigVectorType(this, size, child_type);
  }
  return WrapType(entry);
}

CompilerType
TypeSystemZig::GetFunctionType(llvm::ArrayRef<ZigType *> param_types,
                               bool is_var_args,
                               opaque_compiler_type_t ret_type) {
  for (ZigType *param_type : param_types)
    if (!param_type)
      return WrapType();
  if (!ret_type)
    return WrapType();
  ZigFunctionTypeKeyInfo::Key key(param_types, is_var_args,
                                  UnwrapType(ret_type));
  ZigFunctionType *&entry = *m_function_types.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigFunctionType *>(m_allocator.Allocate(
        ZigFunctionType::totalSizeToAlloc<ZigType *>(param_types.size()),
        alignof(ZigFunctionType)));
    new (entry)
        ZigFunctionType(this, param_types, is_var_args, UnwrapType(ret_type));
  }
  return WrapType(entry);
}

CompilerType
TypeSystemZig::GetGeneratedTagType(ConstString name,
                                   opaque_compiler_type_t backing_int_type,
                                   llvm::ArrayRef<ZigTagField> fields) {
  ZigIntType *zig_backing_int_type =
      llvm::dyn_cast_if_present<ZigIntType>(UnwrapType(backing_int_type));
  if (!zig_backing_int_type)
    return WrapType();
  ZigGeneratedTagType *zig_type =
      static_cast<ZigGeneratedTagType *>(m_allocator.Allocate(
          ZigGeneratedTagType::totalSizeToAlloc<ZigTagField>(fields.size()),
          alignof(ZigGeneratedTagType)));
  new (zig_type) ZigGeneratedTagType(this, name, zig_backing_int_type, fields);
  return WrapType(zig_type);
}

CompilerType TypeSystemZig::GetEnumType(ConstString name,
                                        opaque_compiler_type_t backing_int_type,
                                        llvm::ArrayRef<ZigTagField> fields) {
  ZigIntType *zig_backing_int_type =
      llvm::dyn_cast_if_present<ZigIntType>(UnwrapType(backing_int_type));
  if (!zig_backing_int_type)
    return WrapType();
  ZigEnumType *zig_type = static_cast<ZigEnumType *>(m_allocator.Allocate(
      ZigEnumType::totalSizeToAlloc<ZigTagField>(fields.size()),
      alignof(ZigEnumType)));
  new (zig_type) ZigEnumType(this, name, zig_backing_int_type, fields);
  return WrapType(zig_type);
}
CompilerType TypeSystemZig::GetStructType(ConstString name, uint64_t byte_size,
                                          llvm::Align align) {
  ZigStructType *zig_type = m_allocator.Allocate<ZigStructType>();
  new (zig_type) ZigStructType(this, name, byte_size, align);
  return WrapType(zig_type);
}
CompilerType TypeSystemZig::GetTupleType(llvm::ArrayRef<ZigTupleField> fields,
                                         uint64_t byte_size,
                                         llvm::Align align) {
  ZigTupleTypeKeyInfo::Key key(fields);
  ZigTupleType *&entry = *m_tuple_types.insert_as(nullptr, key).first;
  if (!entry) {
    entry = static_cast<ZigTupleType *>(m_allocator.Allocate(
        ZigTupleType::totalSizeToAlloc<ZigTupleField>(fields.size()),
        alignof(ZigTupleType)));
    new (entry) ZigTupleType(this, fields, byte_size, align);
  }
  return WrapType(entry);
}
CompilerType
TypeSystemZig::GetPackedStructType(ConstString name,
                                   opaque_compiler_type_t backing_int_type) {
  ZigIntType *zig_backing_int_type =
      llvm::dyn_cast_if_present<ZigIntType>(UnwrapType(backing_int_type));
  if (!zig_backing_int_type)
    return WrapType();
  ZigPackedStructType *zig_type = m_allocator.Allocate<ZigPackedStructType>();
  new (zig_type) ZigPackedStructType(this, name, zig_backing_int_type);
  return WrapType(zig_type);
}
CompilerType TypeSystemZig::GetUnionType(ConstString name, uint64_t byte_size,
                                         llvm::Align align) {
  ZigUnionType *zig_type = m_allocator.Allocate<ZigUnionType>();
  new (zig_type) ZigUnionType(this, name, byte_size, align);
  return WrapType(zig_type);
}
CompilerType TypeSystemZig::GetTaggedUnionType(ConstString name,
                                               opaque_compiler_type_t tag_type,
                                               uint32_t tag_byte_offset,
                                               uint64_t byte_size,
                                               llvm::Align align) {
  ZigTagType *zig_tag_type =
      llvm::dyn_cast_if_present<ZigTagType>(UnwrapType(tag_type));
  if (!zig_tag_type)
    return WrapType();
  ZigTaggedUnionType *zig_type = m_allocator.Allocate<ZigTaggedUnionType>();
  new (zig_type) ZigTaggedUnionType(this, name, zig_tag_type, tag_byte_offset,
                                    byte_size, align);
  return WrapType(zig_type);
}
CompilerType
TypeSystemZig::GetPackedUnionType(ConstString name,
                                  opaque_compiler_type_t backing_int_type) {
  ZigIntType *zig_backing_int_type =
      llvm::dyn_cast_if_present<ZigIntType>(UnwrapType(backing_int_type));
  if (!zig_backing_int_type)
    return WrapType();
  ZigPackedUnionType *zig_type = m_allocator.Allocate<ZigPackedUnionType>();
  new (zig_type) ZigPackedUnionType(this, name, zig_backing_int_type);
  return WrapType(zig_type);
}

CompilerType
TypeSystemZig::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                   size_t bit_size) {
  switch (encoding) {
  case eEncodingInvalid:
    break;
  case eEncodingUint:
  case eEncodingSint:
    if (bit_size <= UINT16_MAX)
      return GetIntType(ConstString(), encoding == eEncodingSint, bit_size,
                        IntAbiSize(bit_size, m_arch), std::nullopt);
    break;
  case eEncodingIEEE754:
    switch (bit_size) {
    case 16:
    case 32:
    case 64:
    case 80:
    case 128:
      return GetFloatType(ConstString(), bit_size, IntAbiSize(bit_size, m_arch),
                          FloatAbiAlignment(bit_size, m_arch));
    }
    break;
  case eEncodingVector:
    if (CompilerType int_type =
            GetBuiltinTypeForEncodingAndBitSize(eEncodingUint, bit_size))
      return GetVectorType(1, int_type.GetOpaqueQualType());
    break;
  }
  return WrapType();
}

CompilerDeclContext
TypeSystemZig::GetCompilerDeclContextForType(const CompilerType &type) {
  return type ? WrapScope(UnwrapType(type)->GetNamespace())
              : CompilerDeclContext();
}

ByteOrder TypeSystemZig::GetByteOrder() { return m_arch.GetByteOrder(); }

uint32_t TypeSystemZig::GetPointerByteSize() {
  return m_arch.GetAddressByteSize();
}

DWARFASTParser *TypeSystemZig::GetDWARFParser() {
  if (!m_dwarf_ast_parser_up)
    m_dwarf_ast_parser_up = std::make_unique<DWARFASTParserZig>(*this);
  return m_dwarf_ast_parser_up.get();
}

ConstString TypeSystemZig::DeclGetName(void *opaque_decl) {
  return opaque_decl ? UnwrapDecl(opaque_decl)->GetName() : ConstString();
}

CompilerDeclContext TypeSystemZig::DeclGetDeclContext(void *opaque_decl) {
  return opaque_decl ? WrapScope(UnwrapDecl(opaque_decl)->GetParent())
                     : WrapScope();
}

CompilerType TypeSystemZig::DeclGetFunctionReturnType(void *opaque_decl) {
  if (ZigFunction *zig_function =
          llvm::dyn_cast_if_present<ZigFunction>(UnwrapDecl(opaque_decl)))
    return WrapType(zig_function->GetType()->GetReturnType());
  return WrapType();
}

size_t TypeSystemZig::DeclGetFunctionNumArguments(void *opaque_decl) {
  if (ZigFunction *zig_function =
          llvm::dyn_cast_if_present<ZigFunction>(UnwrapDecl(opaque_decl)))
    return zig_function->GetType()->GetParamTypes().size();
  return 0;
}

CompilerType TypeSystemZig::DeclGetFunctionArgumentType(void *opaque_decl,
                                                        size_t arg_idx) {
  if (ZigFunction *zig_function =
          llvm::dyn_cast_if_present<ZigFunction>(UnwrapDecl(opaque_decl)))
    if (auto zig_param_types = zig_function->GetType()->GetParamTypes();
        arg_idx < zig_param_types.size())
      return WrapType(zig_param_types[arg_idx]);
  return WrapType();
}

std::vector<CompilerContext>
TypeSystemZig::DeclGetCompilerContext(void *opaque_decl) {
  std::vector<CompilerContext> ctx;
  ZigDeclaration *zig_decl = UnwrapDecl(opaque_decl);
  if (!zig_decl)
    return ctx;
  CompilerContextKind kind;
  switch (zig_decl->GetKind()) {
  case ZigValue::Kind::Constant:
    if (llvm::isa<ZigTypeType>(
            llvm::cast<ZigConstant>(zig_decl)->GetValue()->GetType())) {
      [[fallthrough]];
    case ZigValue::Kind::Alias:
      kind = CompilerContextKind::Typedef;
      break;
    }
    [[fallthrough]];
  case ZigValue::Kind::Variable:
    kind = CompilerContextKind::Variable;
    break;
  case ZigValue::Kind::Function:
    kind = CompilerContextKind::Function;
    break;
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    llvm_unreachable("not a decl");
  }
  ctx.emplace_back(kind, zig_decl->GetName());
  for (ZigScope *zig_scope = zig_decl->GetParent(); zig_scope;
       zig_scope = zig_scope->GetParent()) {
    ConstString name = zig_scope->GetName();
    if (name.IsNull())
      continue;
    switch (zig_scope->GetKind()) {
    case ZigScope::Kind::Module:
      kind = CompilerContextKind::Module;
      break;
    case ZigScope::Kind::Namespace:
    case ZigScope::Kind::Container:
      switch (llvm::cast<ZigNamespace>(zig_scope)->GetOwner()->GetKind()) {
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
      case ZigValue::Kind::OptionalType:
      case ZigValue::Kind::PointerType:
      case ZigValue::Kind::ErrorUnionType:
      case ZigValue::Kind::ArrayType:
      case ZigValue::Kind::VectorType:
      case ZigValue::Kind::FunctionType:
      case ZigValue::Kind::ErrorSetType:
        llvm_unreachable("not a namespace type");
      case ZigValue::Kind::GeneratedTagType:
      case ZigValue::Kind::EnumType:
        kind = CompilerContextKind::Enum;
        break;
      case ZigValue::Kind::TupleType:
      case ZigValue::Kind::StructType:
      case ZigValue::Kind::PackedStructType:
        kind = CompilerContextKind::ClassOrStruct;
        break;
      case ZigValue::Kind::UnionType:
      case ZigValue::Kind::TaggedUnionType:
      case ZigValue::Kind::PackedUnionType:
        kind = CompilerContextKind::Union;
        break;
      }
      break;
    case ZigScope::Kind::Function:
      kind = CompilerContextKind::Function;
      break;
    case ZigScope::Kind::InlinedBlock:
    case ZigScope::Kind::Block:
      continue;
    }
    ctx.emplace_back(kind, name);
  }
  std::reverse(ctx.begin(), ctx.end());
  return ctx;
}

Scalar TypeSystemZig::DeclGetConstantValue(void *opaque_decl) {
  ZigConstant *zig_const =
      llvm::dyn_cast_if_present<ZigConstant>(UnwrapDecl(opaque_decl));
  if (!zig_const)
    return Scalar();
  switch (ZigValue *zig_value = zig_const->GetValue(); zig_value->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
  case ZigValue::Kind::Variable:
  case ZigValue::Kind::Function:
    llvm_unreachable("already unwrapped");
  case ZigValue::Kind::Data:
  case ZigValue::Kind::OnlyPossibleValue:
  case ZigValue::Kind::EnumLiteral:
  case ZigValue::Kind::Pointer:
  case ZigValue::Kind::Slice:
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    break;
  case ZigValue::Kind::ComptimeInt:
    return llvm::cast<ZigComptimeInt>(zig_value)->GetValue();
  case ZigValue::Kind::ComptimeFloat:
    return llvm::cast<ZigComptimeFloat>(zig_value)->GetValue();
  case ZigValue::Kind::BoolFalse:
  case ZigValue::Kind::BoolTrue:
    return llvm::cast<ZigBool>(zig_value)->GetValue();
  case ZigValue::Kind::Int:
    return llvm::cast<ZigInt>(zig_value)->GetValue();
  case ZigValue::Kind::Float:
    return llvm::cast<ZigFloat>(zig_value)->GetValue();
  case ZigValue::Kind::Tag:
    return llvm::cast<ZigTag>(zig_value)->GetValue()->GetValue();
  }
  return Scalar();
}

ValueObjectSP
TypeSystemZig::DeclGetConstantValueObject(void *opaque_decl,
                                          ExecutionContextScope *exe_scope) {
  ZigConstant *zig_const =
      llvm::dyn_cast_if_present<ZigConstant>(UnwrapDecl(opaque_decl));
  if (!zig_const)
    return nullptr;
  switch (zig_const->GetValue()->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
  case ZigValue::Kind::Variable:
  case ZigValue::Kind::Function:
    llvm_unreachable("already unwrapped");
  case ZigValue::Kind::Data: {
    if (zig_const->GetType()->HasComptimeState())
      break;
    llvm::ArrayRef<uint8_t> data =
        llvm::cast<ZigData>(zig_const->GetValue())->GetData();
    return ValueObject::CreateValueObjectFromData(
        zig_const->GetName(),
        DataExtractor(data.data(), data.size(), GetByteOrder(),
                      GetPointerByteSize()),
        exe_scope, WrapType(zig_const->GetType()));
  }
  case ZigValue::Kind::OnlyPossibleValue:
  case ZigValue::Kind::ComptimeInt:
  case ZigValue::Kind::ComptimeFloat:
  case ZigValue::Kind::EnumLiteral:
  case ZigValue::Kind::Pointer:
  case ZigValue::Kind::Slice:
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    break;
  case ZigValue::Kind::BoolFalse:
  case ZigValue::Kind::BoolTrue:
    return ValueObject::CreateValueObjectFromBool(
        exe_scope->CalculateTarget(),
        llvm::cast<ZigBool>(zig_const->GetValue())->GetValue(),
        zig_const->GetName());
  case ZigValue::Kind::Int:
    return ValueObject::CreateValueObjectFromAPInt(
        exe_scope->CalculateTarget(),
        llvm::cast<ZigInt>(zig_const->GetValue())->GetValue(),
        WrapType(zig_const->GetType()), zig_const->GetName());
  case ZigValue::Kind::Float:
    return ValueObject::CreateValueObjectFromAPFloat(
        exe_scope->CalculateTarget(),
        llvm::cast<ZigFloat>(zig_const->GetValue())->GetValue(),
        WrapType(zig_const->GetType()), zig_const->GetName());
  case ZigValue::Kind::Tag:
    return ValueObject::CreateValueObjectFromAPInt(
        exe_scope->CalculateTarget(),
        llvm::cast<ZigTag>(zig_const->GetValue())->GetValue()->GetValue(),
        WrapType(zig_const->GetType()), zig_const->GetName());
  }
  return ValueObjectZig::Create(exe_scope, zig_const);
}

CompilerType TypeSystemZig::GetTypeForDecl(void *opaque_decl) {
  return opaque_decl ? WrapType(UnwrapDecl(opaque_decl)->GetType())
                     : WrapType();
}

std::vector<CompilerDecl> TypeSystemZig::DeclContextFindDeclByName(
    void *opaque_decl_ctx, ConstString name, bool ignore_using_decls) {
  std::vector<CompilerDecl> found_decls;
  SymbolFile *symbol_file = GetSymbolFile();
  for (ZigScope *zig_scope = UnwrapScope(opaque_decl_ctx); zig_scope;
       zig_scope = zig_scope->GetParent()) {
    if (symbol_file)
      symbol_file->ParseDeclsForContext(WrapScope(zig_scope));
    for (ZigDeclaration *zig_decl = zig_scope->GetFirstDecl(); zig_decl;
         zig_decl = zig_decl->GetSibling())
      if (zig_decl->GetName() == name)
        found_decls.emplace_back(WrapDecl(zig_decl));
  }
  return found_decls;
}

ConstString TypeSystemZig::DeclContextGetName(void *opaque_decl_ctx) {
  if (ZigScope *zig_scope = UnwrapScope(opaque_decl_ctx))
    return zig_scope->GetName();
  return ConstString();
}

ConstString
TypeSystemZig::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  return opaque_decl_ctx ? UnwrapScope(opaque_decl_ctx)->GetQualifiedName()
                         : ConstString();
}

bool TypeSystemZig::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  llvm_unreachable("unimplemented");
}

bool TypeSystemZig::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  llvm_unreachable("unimplemented");
}

LanguageType TypeSystemZig::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeZig;
}

#ifndef NDEBUG
bool TypeSystemZig::VerifyDeclContext(void *opaque_decl_ctx) {
  return !opaque_decl_ctx ||
         static_cast<ZigScope *>(opaque_decl_ctx)->GetTypeSystem() == this;
}
bool TypeSystemZig::VerifyDecl(void *opaque_decl) {
  return !opaque_decl ||
         static_cast<ZigDeclaration *>(opaque_decl)->GetTypeSystem() == this;
}
bool TypeSystemZig::Verify(opaque_compiler_type_t type) {
  return !type || static_cast<ZigType *>(type)->GetTypeSystem() == this;
}
#endif

bool TypeSystemZig::IsArrayType(opaque_compiler_type_t type,
                                CompilerType *element_type, uint64_t *size,
                                bool *is_incomplete) {
  if (ZigArrayType *zig_type =
          llvm::dyn_cast_if_present<ZigArrayType>(UnwrapType(type))) {
    if (element_type)
      *element_type = WrapType(zig_type->GetChildType());
    if (size)
      *size = zig_type->GetLength();
    if (is_incomplete)
      *is_incomplete = false;
    return true;
  }
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;
  return false;
}

bool TypeSystemZig::IsVectorType(opaque_compiler_type_t type,
                                 CompilerType *element_type, uint64_t *size) {
  if (ZigVectorType *zig_type =
          llvm::dyn_cast_if_present<ZigVectorType>(UnwrapType(type))) {
    if (element_type)
      *element_type = WrapType(zig_type->GetChildType());
    if (size)
      *size = zig_type->GetLength();
    return true;
  }
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  return false;
}

bool TypeSystemZig::IsAggregateType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  ZigType *zig_type = UnwrapType(type);
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
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    return false;
  case ZigValue::Kind::PointerType:
    switch (llvm::cast<ZigPointerType>(zig_type)->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      return false;
    case ZigPointerType::Size::Slice:
      return true;
    }
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    return true;
  }
}

bool TypeSystemZig::IsBeingDefined(opaque_compiler_type_t type) {
  if (ZigRecordType *zig_type =
          llvm::dyn_cast_if_present<ZigRecordType>(UnwrapType(type)))
    return !zig_type->GetFields().data();
  return false;
}

bool TypeSystemZig::IsCharType(opaque_compiler_type_t type) { return false; }

bool TypeSystemZig::IsCompleteType(opaque_compiler_type_t type) {
  return GetCompleteType(type);
}

bool TypeSystemZig::IsConst(opaque_compiler_type_t type) { return false; }

bool TypeSystemZig::IsDefined(opaque_compiler_type_t type) {
  if (ZigRecordType *zig_type =
          llvm::dyn_cast_if_present<ZigRecordType>(UnwrapType(type)))
    return zig_type->GetFields().data();
  return type;
}

bool TypeSystemZig::IsFloatingPointType(opaque_compiler_type_t type,
                                        uint32_t &count, bool &is_complex) {
  ZigType *zig_type = UnwrapType(type);
  if (llvm::isa_and_present<ZigComptimeFloatType>(zig_type) ||
      llvm::isa_and_present<ZigFloatType>(zig_type)) {
    count = 1;
    is_complex = false;
    return true;
  }
  if (ZigVectorType *zig_vector_type =
          llvm::dyn_cast_if_present<ZigVectorType>(zig_type);
      zig_vector_type &&
      llvm::dyn_cast<ZigFloatType>(zig_vector_type->GetChildType())) {
    count = zig_vector_type->GetLength();
    is_complex = false;
    return true;
  }
  count = 0;
  is_complex = false;
  return false;
}

unsigned TypeSystemZig::GetPtrAuthKey(opaque_compiler_type_t type) {
  UnwrapType(type);
  return 0;
}

unsigned TypeSystemZig::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  UnwrapType(type);
  return 0;
}

bool TypeSystemZig::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  UnwrapType(type);
  return false;
}

bool TypeSystemZig::IsFunctionType(opaque_compiler_type_t type) {
  return llvm::isa_and_present<ZigFunctionType>(UnwrapType(type));
}

uint32_t TypeSystemZig::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                               CompilerType *base_type) {
  UnwrapType(type);
  if (base_type)
    base_type->Clear();
  llvm_unreachable("unimplemented");
}

size_t
TypeSystemZig::GetNumberOfFunctionArguments(opaque_compiler_type_t type) {
  if (ZigFunctionType *zig_type =
          llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type)))
    return zig_type->GetParamTypes().size();
  return 0;
}

CompilerType
TypeSystemZig::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                          size_t index) {
  if (ZigFunctionType *zig_type =
          llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type)))
    if (index < zig_type->GetParamTypes().size())
      return WrapType(zig_type->GetParamTypes()[index]);
  return WrapType();
}

bool TypeSystemZig::IsFunctionPointerType(opaque_compiler_type_t type) {
  if (ZigPointerType *zig_type =
          llvm::dyn_cast_if_present<ZigPointerType>(UnwrapType(type)))
    return llvm::isa<ZigFunctionType>(zig_type->GetChildType());
  return false;
}

bool TypeSystemZig::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  UnwrapType(type);
  return false;
}

bool TypeSystemZig::IsBlockPointerType(opaque_compiler_type_t type,
                                       CompilerType *function_pointer_type) {
  UnwrapType(type);
  if (function_pointer_type)
    function_pointer_type->Clear();
  return false;
}

bool TypeSystemZig::IsIntegerType(opaque_compiler_type_t type,
                                  bool &is_signed) {
  ZigType *zig_type = UnwrapType(type);
  if (llvm::isa_and_present<ZigComptimeIntType>(zig_type)) {
    is_signed = true;
    return true;
  }
  if (llvm::isa_and_present<ZigBoolType>(zig_type)) {
    is_signed = false;
    return true;
  }
  if (ZigIntType *int_type = llvm::dyn_cast_if_present<ZigIntType>(zig_type)) {
    is_signed = int_type->IsSigned();
    return true;
  }
  is_signed = false;
  return false;
}

bool TypeSystemZig::IsEnumerationType(opaque_compiler_type_t type,
                                      bool &is_signed) {
  if (ZigTagType *zig_type =
          llvm::dyn_cast_if_present<ZigTagType>(UnwrapType(type))) {
    is_signed = zig_type->GetBackingType()->IsSigned();
    return true;
  }
  is_signed = false;
  return false;
}

bool TypeSystemZig::IsPolymorphicClass(opaque_compiler_type_t type) {
  UnwrapType(type);
  return false;
}

bool TypeSystemZig::IsPossibleDynamicType(opaque_compiler_type_t type,
                                          CompilerType *target_type,
                                          bool check_cplusplus,
                                          bool check_objc) {
  UnwrapType(type);
  // reevaluate when zig has struct safety tags
  if (target_type)
    target_type->Clear();
  return false;
}

bool TypeSystemZig::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  UnwrapType(type);
  return false;
}

bool TypeSystemZig::IsPointerType(opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  if (ZigPointerType *zig_type =
          llvm::dyn_cast_if_present<ZigPointerType>(UnwrapType(type)))
    switch (zig_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      if (pointee_type)
        *pointee_type = WrapType(zig_type->GetChildType());
      return true;
    case ZigPointerType::Size::Slice:
      break;
    }
  if (pointee_type)
    pointee_type->Clear();
  return false;
}

bool TypeSystemZig::IsScalarType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  switch (UnwrapType(type)->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias:
  case ZigValue::Kind::Variable:
  case ZigValue::Kind::Function:
  case ZigValue::Kind::Data:
  case ZigValue::Kind::OnlyPossibleValue:
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    return false;
  case ZigValue::Kind::ComptimeInt:
  case ZigValue::Kind::ComptimeFloat:
  case ZigValue::Kind::BoolType:
  case ZigValue::Kind::IntType:
  case ZigValue::Kind::FloatType:
    return true;
  }
}

bool TypeSystemZig::IsVoidType(opaque_compiler_type_t type) {
  return llvm::isa_and_present<ZigVoidType>(UnwrapType(type));
}

bool TypeSystemZig::CanPassInRegisters(const CompilerType &type) {
  return true;
}

bool TypeSystemZig::SupportsLanguage(LanguageType language) {
  return TypeSystemZigSupportsLanguage(language);
}

bool TypeSystemZig::GetCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  SymbolFile *symbol_file = GetSymbolFile();
  if (!symbol_file)
    return true;
  CompilerType compiler_type = WrapType(UnwrapType(type));
  return symbol_file->CompleteType(compiler_type);
}

ConstString TypeSystemZig::GetTypeName(opaque_compiler_type_t type,
                                       bool base_only) {
  if (!type)
    return ConstString();
  ZigType *zig_type = UnwrapType(type);
  return base_only ? zig_type->GetName() : zig_type->GetQualifiedName();
}

ConstString TypeSystemZig::GetDisplayTypeName(opaque_compiler_type_t type) {
  return GetTypeName(type, false);
}

uint32_t
TypeSystemZig::GetTypeInfo(opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  if (!type)
    return 0;
  ZigType *zig_type = UnwrapType(type);
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
    return eTypeHasChildren | eTypeIsBuiltIn;
  case ZigValue::Kind::VoidType:
  case ZigValue::Kind::NoReturnType:
  case ZigValue::Kind::UndefinedType:
  case ZigValue::Kind::NullType:
  case ZigValue::Kind::AnyOpaqueType:
  case ZigValue::Kind::EnumLiteralType:
  case ZigValue::Kind::AnyType:
    return eTypeHasValue | eTypeIsBuiltIn;
  case ZigValue::Kind::IntType:
    if (llvm::cast<ZigIntType>(zig_type)->IsSigned()) {
      [[fallthrough]];
    case ZigValue::Kind::ComptimeIntType:
      return eTypeHasValue | eTypeIsBuiltIn | eTypeIsScalar | eTypeIsInteger |
             eTypeIsSigned;
    } else {
      [[fallthrough]];
    case ZigValue::Kind::BoolType:
      return eTypeHasValue | eTypeIsBuiltIn | eTypeIsScalar | eTypeIsInteger;
    }
  case ZigValue::Kind::ComptimeFloatType:
  case ZigValue::Kind::FloatType:
    return eTypeHasValue | eTypeIsBuiltIn | eTypeIsScalar | eTypeIsFloat;
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    return eTypeHasChildren | eTypeIsStructUnion;
  case ZigValue::Kind::PointerType: {
    ZigPointerType *zig_pointer_type = llvm::cast<ZigPointerType>(zig_type);
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          WrapType(zig_pointer_type->GetChildType());
    switch (zig_pointer_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      return eTypeHasValue | eTypeHasChildren | eTypeIsPointer;
    case ZigPointerType::Size::Slice:
      return eTypeHasChildren | eTypeIsStructUnion;
    }
  }
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          WrapType(llvm::cast<ZigSequenceType>(zig_type)->GetChildType());
    return eTypeHasChildren |
           (zig_type->GetKind() == ZigValue::Kind::ArrayType ? eTypeIsArray
                                                             : eTypeIsVector);
  case ZigValue::Kind::FunctionType:
    return eTypeHasValue | eTypeIsFuncPrototype;
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    if (pointee_or_element_compiler_type)
      *pointee_or_element_compiler_type =
          WrapType(llvm::cast<ZigTagType>(zig_type)->GetBackingType());
    return eTypeHasValue | eTypeIsEnumeration;
  }
}

LanguageType TypeSystemZig::GetMinimumLanguage(opaque_compiler_type_t type) {
  UnwrapType(type);
  return eLanguageTypeZig;
}

TypeClass TypeSystemZig::GetTypeClass(opaque_compiler_type_t type) {
  if (!type)
    return eTypeClassInvalid;
  ZigType *zig_type = UnwrapType(type);
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
    return eTypeClassBuiltin;
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
    return eTypeClassUnion;
  case ZigValue::Kind::PointerType:
    switch (llvm::cast<ZigPointerType>(zig_type)->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      return eTypeClassPointer;
    case ZigPointerType::Size::Slice:
      return eTypeClassStruct;
    }
  case ZigValue::Kind::ArrayType:
    return eTypeClassArray;
  case ZigValue::Kind::VectorType:
    return eTypeClassVector;
  case ZigValue::Kind::FunctionType:
    return eTypeClassFunction;
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    return eTypeClassEnumeration;
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
    return eTypeClassStruct;
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    return eTypeClassUnion;
  }
}

unsigned TypeSystemZig::GetTypeQualifiers(opaque_compiler_type_t type) {
  UnwrapType(type);
  return 0;
}

CompilerType
TypeSystemZig::GetArrayElementType(opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  if (ZigArrayType *zig_type =
          llvm::dyn_cast_if_present<ZigArrayType>(UnwrapType(type)))
    return WrapType(zig_type->GetChildType());
  return WrapType();
}

CompilerType TypeSystemZig::GetCanonicalType(opaque_compiler_type_t type) {
  llvm_unreachable("unimplemented");
}

CompilerType
TypeSystemZig::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  return WrapType(UnwrapType(type));
}

CompilerType
TypeSystemZig::GetEnumerationIntegerType(opaque_compiler_type_t type) {
  if (ZigEnumType *zig_type =
          llvm::dyn_cast_if_present<ZigEnumType>(UnwrapType(type)))
    return WrapType(zig_type->GetBackingType());
  return WrapType();
}

int TypeSystemZig::GetFunctionArgumentCount(opaque_compiler_type_t type) {
  if (ZigFunctionType *zig_type =
          llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type)))
    return zig_type->GetParamTypes().size();
  return -1;
}

CompilerType
TypeSystemZig::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                              size_t idx) {
  if (ZigFunctionType *zig_type =
          llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type)))
    if (auto zig_param_types = zig_type->GetParamTypes();
        idx < zig_param_types.size())
      return WrapType(zig_param_types[idx]);
  return WrapType();
}

CompilerType TypeSystemZig::GetFunctionReturnType(opaque_compiler_type_t type) {
  if (ZigFunctionType *zig_type =
          llvm::dyn_cast_if_present<ZigFunctionType>(UnwrapType(type)))
    return WrapType(zig_type->GetReturnType());
  return WrapType();
}

size_t TypeSystemZig::GetNumMemberFunctions(opaque_compiler_type_t type) {
  llvm_unreachable("unimplemented");
}

TypeMemberFunctionImpl
TypeSystemZig::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                        size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType TypeSystemZig::GetPointeeType(opaque_compiler_type_t type) {
  if (ZigPointerType *zig_type =
          llvm::dyn_cast_if_present<ZigPointerType>(UnwrapType(type)))
    switch (zig_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      return WrapType(zig_type->GetChildType());
    case ZigPointerType::Size::Slice:
      break;
    }
  return WrapType();
}

CompilerType TypeSystemZig::GetNonReferenceType(opaque_compiler_type_t type) {
  return WrapType(UnwrapType(type));
}

CompilerType TypeSystemZig::GetPointerType(opaque_compiler_type_t type) {
  return GetPointerType(ZigPointerType::Size::C, nullptr, true, std::nullopt,
                        ZigPointerType::AddressSpace::Generic, false, false,
                        type);
}

CompilerType TypeSystemZig::GetTypedefedType(opaque_compiler_type_t type) {
  llvm_unreachable("unimplemented");
}

CompilerType TypeSystemZig::GetBasicTypeFromAST(BasicType basic_type) {
  const clang::TargetInfo *target_info = GetTargetInfo();
  using IntType = clang::TargetInfo::IntType;
  ConstString name;
  std::optional<bool> is_signed;
  IntType int_type;
  switch (basic_type) {
  case eBasicTypeInvalid:
  case eBasicTypeFloatComplex:
  case eBasicTypeDoubleComplex:
  case eBasicTypeLongDoubleComplex:
  case eBasicTypeObjCID:
  case eBasicTypeObjCClass:
  case eBasicTypeObjCSel:
  case eBasicTypeOther:
    return WrapType();
  case eBasicTypeVoid:
    return GetVoidType();
  case eBasicTypeChar:
    name.SetString("c_char");
    is_signed = m_arch.CharIsSignedByDefault();
    int_type = IntType::SignedChar;
    break;
  case eBasicTypeSignedChar:
    int_type = IntType::SignedChar;
    break;
  case eBasicTypeUnsignedChar:
  case eBasicTypeChar8:
    int_type = IntType::UnsignedChar;
    break;
  case eBasicTypeWChar:
  case eBasicTypeSignedWChar:
  case eBasicTypeUnsignedWChar:
    if (!target_info)
      return WrapType();
    switch (basic_type) {
    default:
      llvm_unreachable("already checked");
    case eBasicTypeWChar:
      break;
    case eBasicTypeSignedWChar:
      is_signed = true;
      break;
    case eBasicTypeUnsignedWChar:
      is_signed = false;
      break;
    }
    int_type = target_info->getWCharType();
    break;
  case eBasicTypeChar16:
    if (!target_info)
      return WrapType();
    int_type = target_info->getChar16Type();
    break;
  case eBasicTypeChar32:
    if (!target_info)
      return WrapType();
    int_type = target_info->getChar32Type();
    break;
  case eBasicTypeShort:
    name.SetString("c_short");
    int_type = IntType::SignedShort;
    break;
  case eBasicTypeUnsignedShort:
    name.SetString("c_ushort");
    int_type = IntType::UnsignedShort;
    break;
  case eBasicTypeInt:
    name.SetString("c_int");
    int_type = IntType::SignedInt;
    break;
  case eBasicTypeUnsignedInt:
    name.SetString("c_uint");
    int_type = IntType::UnsignedInt;
    break;
  case eBasicTypeLong:
    name.SetString("c_long");
    int_type = IntType::SignedLong;
    break;
  case eBasicTypeUnsignedLong:
    name.SetString("c_ulong");
    int_type = IntType::UnsignedLong;
    break;
  case eBasicTypeLongLong:
    name.SetString("c_longlong");
    int_type = IntType::SignedLongLong;
    break;
  case eBasicTypeUnsignedLongLong:
    name.SetString("c_ulonglong");
    int_type = IntType::UnsignedLongLong;
    break;
  case eBasicTypeInt128:
  case eBasicTypeUnsignedInt128:
    return GetIntType(name, basic_type == eBasicTypeInt128, 128, 16,
                      target_info ? llvm::Align(target_info->getInt128Align())
                                  : IntAbiAlignment(128, m_arch));
  case eBasicTypeBool:
    return GetBoolType();
  case eBasicTypeHalf:
  case eBasicTypeFloat:
  case eBasicTypeDouble:
  case eBasicTypeLongDouble: {
    unsigned bit_size;
    unsigned bit_align;
    switch (basic_type) {
    default:
      llvm_unreachable("already checked");
    case eBasicTypeHalf:
      bit_size = target_info ? target_info->getHalfWidth() : 16;
      bit_align = target_info ? target_info->getHalfAlign() : 16;
      break;
    case eBasicTypeFloat:
      bit_size = target_info ? target_info->getFloatWidth() : 32;
      bit_align = target_info ? target_info->getFloatAlign() : 32;
      break;
    case eBasicTypeDouble:
      bit_size = target_info ? target_info->getDoubleWidth() : 64;
      bit_align = target_info ? target_info->getDoubleAlign() : 64;
      break;
    case eBasicTypeLongDouble:
      name.SetString("c_longdouble");
      bit_size = target_info ? target_info->getLongDoubleWidth() : 64;
      bit_align = target_info ? target_info->getLongDoubleAlign() : 64;
      break;
    }
    unsigned byte_size = llvm::divideCeil(bit_size, 8);
    llvm::Align align(llvm::divideCeil(bit_align, 8));
    return GetFloatType(name, bit_size, byte_size, align);
  }
  case eBasicTypeNullPtr:
    return GetNullType();
  }
  if (!target_info)
    return WrapType();
  unsigned bit_size = target_info->getTypeWidth(int_type);
  unsigned byte_size = llvm::divideCeil(bit_size, 8);
  llvm::Align align(llvm::divideCeil(target_info->getTypeAlign(int_type), 8));
  return GetIntType(
      name, is_signed.value_or(clang::TargetInfo::isTypeSigned(int_type)),
      bit_size, byte_size, align);
}

const llvm::fltSemantics &
TypeSystemZig::GetFloatTypeSemantics(size_t byte_size) {
  if (byte_size == 12)
    byte_size = 10;
  return ZigFloatType::GetSemantics(byte_size * 8);
}

std::optional<uint64_t>
TypeSystemZig::GetByteSize(opaque_compiler_type_t type,
                           ExecutionContextScope *exe_scope) {
  if (ZigType *zig_type = UnwrapType(type))
    return zig_type->GetByteSize();
  return std::nullopt;
}
std::optional<uint64_t>
TypeSystemZig::GetBitSize(opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  if (ZigType *zig_type = UnwrapType(type))
    return zig_type->GetBitSize();
  return std::nullopt;
}

Encoding TypeSystemZig::GetEncoding(opaque_compiler_type_t type,
                                    uint64_t &count) {
  count = 0;
  if (!type)
    return eEncodingInvalid;

  ZigType *zig_type = UnwrapType(type);
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
    return eEncodingInvalid;
  case ZigValue::Kind::BoolType:
    count = 1;
    return eEncodingUint;
  case ZigValue::Kind::IntType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::PackedUnionType: {
    ZigIntType *zig_backing_type;
    switch (zig_type->GetKind()) {
    default:
      llvm_unreachable("already checked");
    case ZigValue::Kind::IntType:
      zig_backing_type = llvm::cast<ZigIntType>(zig_type);
      break;
    case ZigValue::Kind::ErrorSetType:
    case ZigValue::Kind::GeneratedTagType:
    case ZigValue::Kind::EnumType:
      zig_backing_type = llvm::cast<ZigTagType>(zig_type)->GetBackingType();
      break;
    case ZigValue::Kind::PackedStructType:
      zig_backing_type =
          llvm::cast<ZigPackedStructType>(zig_type)->GetBackingType();
      break;
    case ZigValue::Kind::PackedUnionType:
      zig_backing_type =
          llvm::cast<ZigPackedUnionType>(zig_type)->GetBackingType();
      break;
    }
    count = 1;
    return zig_backing_type->IsSigned() ? eEncodingSint : eEncodingUint;
  }
  case ZigValue::Kind::FloatType:
    count = 1;
    return eEncodingIEEE754;
  case ZigValue::Kind::PointerType:
    switch (llvm::cast<ZigPointerType>(zig_type)->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      count = 1;
      return eEncodingUint;
    case ZigPointerType::Size::Slice:
      return eEncodingInvalid;
    }
  }
}

Format TypeSystemZig::GetFormat(opaque_compiler_type_t type) {
  if (!type)
    return eFormatInvalid;

  ZigType *zig_type = UnwrapType(type);
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
    return eFormatVoid;
  case ZigValue::Kind::BoolType:
    return eFormatBoolean;
  case ZigValue::Kind::IntType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::PackedUnionType: {
    ZigIntType *zig_backing_type;
    switch (zig_type->GetKind()) {
    default:
      llvm_unreachable("already checked");
    case ZigValue::Kind::IntType:
      zig_backing_type = llvm::cast<ZigIntType>(zig_type);
      break;
    case ZigValue::Kind::PackedStructType:
      zig_backing_type =
          llvm::cast<ZigPackedStructType>(zig_type)->GetBackingType();
      break;
    case ZigValue::Kind::PackedUnionType:
      zig_backing_type =
          llvm::cast<ZigPackedUnionType>(zig_type)->GetBackingType();
      break;
    }
    return zig_backing_type->IsSigned() ? eFormatDecimal : eFormatUnsigned;
  }
  case ZigValue::Kind::FloatType:
    return eFormatFloat;
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
    return eFormatInvalid;
  case ZigValue::Kind::PointerType:
    switch (llvm::cast<ZigPointerType>(zig_type)->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      return eFormatHex;
    case ZigPointerType::Size::Slice:
      return eFormatInvalid;
    }
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    return eFormatEnum;
  }
}

std::optional<size_t>
TypeSystemZig::GetTypeBitAlign(opaque_compiler_type_t type,
                               ExecutionContextScope *exe_scope) {
  if (type)
    return UnwrapType(type)->GetAlign().value() * 8;
  return std::nullopt;
}

llvm::Expected<uint32_t>
TypeSystemZig::GetNumChildren(opaque_compiler_type_t type,
                              bool omit_empty_base_classes,
                              const ExecutionContext *exe_ctx) {
  if (!type)
    return llvm::createStringError("invalid zig type");

  uint64_t num_children = 0;
  ZigType *zig_type = UnwrapType(type);
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
    llvm_unreachable("handled by ValueObjectZig");
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
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    break;
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
    num_children = 2;
    break;
  case ZigValue::Kind::PointerType: {
    ZigPointerType *zig_pointer_type = llvm::cast<ZigPointerType>(zig_type);
    switch (zig_pointer_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::C:
      num_children = 1;
      break;
    case ZigPointerType::Size::Many:
      break;
    case ZigPointerType::Size::Slice:
      num_children = 2;
      break;
    }
    break;
  }
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
    num_children = llvm::cast<ZigSequenceType>(zig_type)->GetLength();
    break;
  case ZigValue::Kind::TupleType:
    num_children = llvm::cast<ZigTupleType>(zig_type)->GetFields().size();
    break;
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::PackedUnionType:
    num_children = llvm::cast<ZigRecordType>(zig_type)->GetFields().size();
    break;
  case ZigValue::Kind::TaggedUnionType:
    num_children =
        1 + llvm::cast<ZigTaggedUnionType>(zig_type)->GetFields().size();
    break;
  }
  return std::min<uint64_t>(num_children, UINT32_MAX);
}

BasicType TypeSystemZig::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  if (!type)
    return eBasicTypeInvalid;
  ZigType *zig_type = UnwrapType(type);
  switch (zig_type->GetKind()) {
  default:
    break;
  case ZigValue::Kind::VoidType:
    return eBasicTypeVoid;
  case ZigValue::Kind::NullType:
    return eBasicTypeNullPtr;
  case ZigValue::Kind::BoolType:
    return eBasicTypeBool;
  case ZigValue::Kind::IntType:
    if (zig_type->GetName() == "c_char")
      return eBasicTypeChar;
    if (zig_type->GetName() == "c_short")
      return eBasicTypeShort;
    if (zig_type->GetName() == "c_ushort")
      return eBasicTypeUnsignedShort;
    if (zig_type->GetName() == "c_int")
      return eBasicTypeInt;
    if (zig_type->GetName() == "c_uint")
      return eBasicTypeUnsignedInt;
    if (zig_type->GetName() == "c_long")
      return eBasicTypeLong;
    if (zig_type->GetName() == "c_ulong")
      return eBasicTypeUnsignedLong;
    if (zig_type->GetName() == "c_longlong")
      return eBasicTypeLongLong;
    if (zig_type->GetName() == "c_ulonglong")
      return eBasicTypeUnsignedLongLong;
    break;
  case ZigValue::Kind::FloatType:
    if (zig_type->GetName() == "c_longdouble")
      return eBasicTypeLongDouble;
    break;
  }
  return eBasicTypeInvalid;
}

void TypeSystemZig::ForEachEnumerator(
    opaque_compiler_type_t type,
    std::function<bool(const CompilerType &int_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  if (ZigTagType *zig_tag_type =
          llvm::dyn_cast_if_present<ZigTagType>(UnwrapType(type)))
    for (const ZigTagField &field : zig_tag_type->GetFields())
      if (!callback(WrapType(field.GetValue()->GetType()), field.GetName(),
                    field.GetValue()->GetValue()))
        return;
}

uint32_t TypeSystemZig::GetNumFields(opaque_compiler_type_t type) {
  ZigType *zig_type = UnwrapType(type);
  if (llvm::isa_and_present<ZigOptionalType>(zig_type) ||
      llvm::isa_and_present<ZigErrorUnionType>(zig_type))
    return 2;
  else if (ZigPointerType *zig_pointer_type =
               llvm::dyn_cast_if_present<ZigPointerType>(zig_type))
    switch (zig_pointer_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      break;
    case ZigPointerType::Size::Slice:
      return 2;
    }
  else if (ZigTupleType *zig_tuple_type =
               llvm::dyn_cast_if_present<ZigTupleType>(zig_type))
    return zig_tuple_type->GetFields().size();
  else if (ZigRecordType *zig_record_type =
               llvm::dyn_cast_if_present<ZigRecordType>(zig_type))
    return zig_record_type->GetFields().size();
  return 0;
}

CompilerType TypeSystemZig::GetFieldAtIndex(opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset,
                                            uint32_t *bitfield_bit_size,
                                            bool *is_bitfield) {
  if (bit_offset)
    *bit_offset = 0;
  if (bitfield_bit_size)
    *bitfield_bit_size = 0;
  if (is_bitfield)
    *is_bitfield = false;
  ZigType *zig_type = UnwrapType(type);
  if (ZigOptionalType *zig_opt_type =
          llvm::dyn_cast_if_present<ZigOptionalType>(zig_type)) {
    ZigType *zig_child_type = zig_opt_type->GetChildType();
    switch (idx) {
    case 0:
      name = "has_value";
      if (llvm::isa<ZigErrorSetType>(zig_child_type))
        return WrapType(*m_int_types.find_as(
            ZigIntTypeKeyInfo::Key(false, zig_child_type->GetBitSize())));
      if (ZigPointerType *zig_child_ptr_type =
              llvm::dyn_cast<ZigPointerType>(zig_child_type))
        if (!zig_child_ptr_type->IsAllowZero())
          return GetSizeType(false);
      if (bit_offset)
        *bit_offset = UINT64_C(8) * zig_child_type->GetByteSize();
      return WrapType(m_bool_type);
    case 1:
      name = "?";
      return WrapType(zig_child_type);
    default:
      break;
    }
  } else if (ZigPointerType *zig_ptr_type =
                 llvm::dyn_cast_if_present<ZigPointerType>(zig_type)) {
    switch (zig_ptr_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      break;
    case ZigPointerType::Size::Slice:
      switch (idx) {
      case 0:
        name = "ptr";
        return GetPointerType(
            ZigPointerType::Size::Many, zig_ptr_type->GetSentinel(),
            zig_ptr_type->IsAllowZero(), zig_ptr_type->GetPointerAlign(),
            zig_ptr_type->GetAddressSpace(), zig_ptr_type->IsConst(),
            zig_ptr_type->IsVolatile(), zig_ptr_type->GetChildType());
      case 1: {
        CompilerType usize_type = GetSizeType(false);
        if (bit_offset)
          *bit_offset = UnwrapType(usize_type)->GetBitSize();
        name = "len";
        return usize_type;
      }
      default:
        break;
      }
    }
  } else if (ZigErrorUnionType *zig_eu_type =
                 llvm::dyn_cast_if_present<ZigErrorUnionType>(zig_type)) {
    switch (idx) {
    case 0:
      name = "error";
      if (bit_offset && zig_eu_type->GetErrorSet()->GetAlign() <=
                            zig_eu_type->GetPayload()->GetAlign())
        *bit_offset = alignTo(zig_eu_type->GetPayload()->GetByteSize(),
                              zig_eu_type->GetErrorSet()->GetAlign()) *
                      UINT64_C(8);
      return WrapType(zig_eu_type->GetErrorSet());
    case 1:
      name = "value";
      if (bit_offset && zig_eu_type->GetErrorSet()->GetAlign() >
                            zig_eu_type->GetPayload()->GetAlign())
        *bit_offset = alignTo(zig_eu_type->GetErrorSet()->GetByteSize(),
                              zig_eu_type->GetPayload()->GetAlign()) *
                      UINT64_C(8);
      return WrapType(zig_eu_type->GetPayload());
    default:
      break;
    }
  } else if (ZigTupleType *zig_tuple_type =
                 llvm::dyn_cast_if_present<ZigTupleType>(zig_type)) {
    if (llvm::ArrayRef<ZigTupleField> fields = zig_tuple_type->GetFields();
        idx < fields.size()) {
      ZigType *field_type = fields[idx].GetType();
      name = llvm::formatv("{0:d}", idx);
      if (bit_offset) {
        uint64_t byte_offset = 0;
        for (const ZigTupleField &field : fields.take_front(idx))
          byte_offset = alignTo(byte_offset, field.GetType()->GetAlign()) +
                        field.GetType()->GetByteSize();
        *bit_offset =
            UINT64_C(8) * alignTo(byte_offset, field_type->GetAlign());
      }
      return WrapType(field_type);
    }
  } else if (ZigRecordType *zig_type =
                 llvm::dyn_cast_if_present<ZigRecordType>(UnwrapType(type)))
    if (idx < zig_type->GetFields().size()) {
      const ZigRecordField &field = zig_type->GetFields()[idx];
      name = field.GetName().GetString();
      if (bit_offset)
        *bit_offset = field.GetBitOffset();
      return WrapType(field.GetType());
    }
  name.clear();
  return WrapType();
}

uint32_t TypeSystemZig::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  UnwrapType(type);
  return 0;
}

uint32_t TypeSystemZig::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  UnwrapType(type);
  return 0;
}

CompilerType
TypeSystemZig::GetDirectBaseClassAtIndex(opaque_compiler_type_t type,
                                         size_t idx, uint32_t *bit_offset) {
  UnwrapType(type);
  if (bit_offset)
    *bit_offset = 0;
  return WrapType();
}

CompilerType
TypeSystemZig::GetVirtualBaseClassAtIndex(opaque_compiler_type_t type,
                                          size_t idx, uint32_t *bit_offset) {
  UnwrapType(type);
  if (bit_offset)
    *bit_offset = 0;
  return WrapType();
}

CompilerDecl TypeSystemZig::GetStaticFieldWithName(opaque_compiler_type_t type,
                                                   llvm::StringRef name) {
  if (ZigType *zig_type = UnwrapType(type))
    if (ZigScope *zig_scope = zig_type->GetNamespace())
      if (std::vector<CompilerDecl> decls = DeclContextFindDeclByName(
              zig_scope->AsOpaqueDeclContext(), ConstString(name), false);
          decls.size() == 1)
        return decls[0];
  return WrapDecl();
}

llvm::Expected<CompilerType> TypeSystemZig::GetChildCompilerTypeAtIndex(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name, uint64_t &child_bit_size,
    int64_t &child_bit_offset, uint32_t &child_bitfield_bit_size,
    uint32_t &child_bitfield_bit_offset, bool &child_is_base_class,
    bool &child_is_deref_of_parent, ValueObject *valobj,
    uint64_t &language_flags) {
  if (!type)
    return WrapType();

  child_name.clear();
  child_bit_size = 0;
  child_bit_offset = 0;
  child_bitfield_bit_size = 0;
  child_bitfield_bit_offset = 0;
  child_is_base_class = false;
  child_is_deref_of_parent = false;
  language_flags = 0;

  llvm::Expected<uint32_t> num_children_or_err =
      GetNumChildren(type, omit_empty_base_classes, exe_ctx);
  if (!num_children_or_err)
    return num_children_or_err.takeError();

  ZigType *zig_type = UnwrapType(type);
  const bool idx_is_valid = idx < *num_children_or_err;
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
    llvm_unreachable("handled by ValueObjectZig");
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
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    break;
  case ZigValue::Kind::OptionalType:
    if (idx_is_valid) {
      ZigOptionalType *optional_type = llvm::cast<ZigOptionalType>(zig_type);
      ZigType *child_type = optional_type->GetChildType();
      switch (idx) {
      case 0:
        child_name = ".has_value";
        if (llvm::isa<ZigErrorSetType>(child_type)) {
          child_bit_size = child_type->GetBitSize();
          return WrapType(*m_int_types.find_as(
              ZigIntTypeKeyInfo::Key(false, child_bit_size)));
        }
        if (ZigPointerType *child_pointer_type =
                llvm::dyn_cast<ZigPointerType>(child_type))
          if (!child_pointer_type->IsAllowZero()) {
            ZigType *usize_type = UnwrapType(GetSizeType(false));
            child_bit_size = usize_type->GetBitSize();
            return WrapType(usize_type);
          }
        child_bit_offset = UINT64_C(8) * child_type->GetByteSize();
        child_bit_size = 1;
        return WrapType(m_bool_type);
      case 1:
        if (valobj)
          child_name = valobj->GetName().GetStringRef();
        child_name += ".?";
        child_bit_size = UINT64_C(8) * child_type->GetByteSize();
        return WrapType(child_type);
      default:
        llvm_unreachable("index was valid");
      }
    }
    break;
  case ZigValue::Kind::ErrorUnionType:
    if (idx_is_valid) {
      ZigErrorUnionType *error_union_type =
          llvm::cast<ZigErrorUnionType>(zig_type);
      switch (idx) {
      case 0:
        child_name = ".error";
        if (error_union_type->GetErrorSet()->GetAlign() <=
            error_union_type->GetPayload()->GetAlign())
          child_bit_offset =
              UINT64_C(8) *
              alignTo(error_union_type->GetPayload()->GetByteSize(),
                      error_union_type->GetErrorSet()->GetAlign());
        child_bit_size =
            UINT64_C(8) * error_union_type->GetErrorSet()->GetByteSize();
        return WrapType(error_union_type->GetErrorSet());
      case 1:
        child_name = ".value";
        if (error_union_type->GetErrorSet()->GetAlign() >
            error_union_type->GetPayload()->GetAlign())
          child_bit_offset =
              UINT64_C(8) *
              alignTo(error_union_type->GetErrorSet()->GetByteSize(),
                      error_union_type->GetPayload()->GetAlign());
        child_bit_size =
            UINT64_C(8) * error_union_type->GetPayload()->GetByteSize();
        return WrapType(error_union_type->GetPayload());
      default:
        llvm_unreachable("index was valid");
      }
    }
    break;
  case ZigValue::Kind::PointerType: {
    ZigPointerType *pointer_type = llvm::cast<ZigPointerType>(zig_type);
    ZigType *child_type = pointer_type->GetChildType();
    switch (pointer_type->GetSize()) {
    case ZigPointerType::Size::One:
      if (idx == 0) {
        child_is_deref_of_parent = true;
        if (valobj)
          child_name = valobj->GetName().GetStringRef();
        child_name += ".*";
        child_bit_size = child_type->GetBitSize();
        return WrapType(child_type);
      }
      break;
    case ZigPointerType::Size::Many:
    case ZigPointerType::Size::C:
      child_is_deref_of_parent = true;
      child_name += llvm::formatv("[{0:d}]", idx);
      child_bit_size = UINT64_C(8) * child_type->GetByteSize();
      child_bit_offset = idx * child_bit_size;
      return WrapType(child_type);
    case ZigPointerType::Size::Slice:
      if (idx_is_valid) {
        ZigType *usize_type = UnwrapType(GetSizeType(false));
        child_bit_size = usize_type->GetBitSize();
        switch (idx) {
        case 0:
          child_name = ".ptr";
          return GetPointerType(
              ZigPointerType::Size::Many, pointer_type->GetSentinel(),
              pointer_type->IsAllowZero(), pointer_type->GetPointerAlign(),
              pointer_type->GetAddressSpace(), pointer_type->IsConst(),
              pointer_type->IsVolatile(), child_type);
        case 1:
          child_name = ".len";
          child_bit_offset = child_bit_size;
          return WrapType(usize_type);
        default:
          llvm_unreachable("index was valid");
        }
      }
      break;
    }
    break;
  }
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
    if (idx_is_valid || (zig_type->GetKind() == ZigValue::Kind::ArrayType &&
                         ignore_array_bounds)) {
      ZigType *child_type =
          llvm::cast<ZigSequenceType>(zig_type)->GetChildType();
      child_name = llvm::formatv("[{0:d}]", idx);
      child_bit_size = UINT64_C(8) * child_type->GetByteSize();
      child_bit_offset = idx * child_bit_size;
      return WrapType(child_type);
    }
    break;
  case ZigValue::Kind::TupleType:
    if (idx_is_valid) {
      llvm::ArrayRef<ZigTupleField> fields =
          llvm::cast<ZigTupleType>(zig_type)->GetFields();
      uint64_t child_byte_offset = 0;
      for (const ZigTupleField &field : fields.take_front(idx))
        child_byte_offset =
            alignTo(child_byte_offset, field.GetType()->GetAlign()) +
            field.GetType()->GetByteSize();
      ZigType *field_type = fields[idx].GetType();
      child_byte_offset = alignTo(child_byte_offset, field_type->GetAlign());
      child_name = llvm::formatv("[{0:d}]", idx);
      child_bit_size = UINT64_C(8) * field_type->GetByteSize();
      child_bit_offset = UINT64_C(8) * child_byte_offset;
      return WrapType(field_type);
    }
    break;
  case ZigValue::Kind::TaggedUnionType:
    if (idx == 0) {
      ZigTaggedUnionType *tagged_union_type =
          llvm::cast<ZigTaggedUnionType>(zig_type);
      child_name = ".tag";
      child_bit_offset = tagged_union_type->GetTagBitOffset();
      child_bit_size =
          UINT64_C(8) * tagged_union_type->GetTagType()->GetByteSize();
      return WrapType(tagged_union_type->GetTagType());
    }
    --idx;
    [[fallthrough]];
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::PackedUnionType:
    if (idx_is_valid) {
      const ZigRecordField &field =
          llvm::cast<ZigRecordType>(zig_type)->GetFields()[idx];
      ZigType *field_type = field.GetType();
      child_name = ChildFieldName(field.GetName()).GetStringRef();
      switch (zig_type->GetKind()) {
      default:
        llvm_unreachable("already checked");
      case ZigValue::Kind::StructType:
      case ZigValue::Kind::UnionType:
      case ZigValue::Kind::TaggedUnionType:
        child_bit_size = UINT64_C(8) * field_type->GetByteSize();
        break;
      case ZigValue::Kind::PackedStructType:
      case ZigValue::Kind::PackedUnionType:
        child_bit_size = field_type->GetBitSize();
        break;
      }
      child_bit_offset = field.GetBitOffset();
      return WrapType(field_type);
    }
    break;
  }
  return WrapType();
}

uint32_t TypeSystemZig::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                                llvm::StringRef name,
                                                bool omit_empty_base_classes) {
  if (!type || name.empty())
    return UINT32_MAX;

  ZigType *zig_type = UnwrapType(type);
  uint32_t idx;
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
    llvm_unreachable("handled by ValueObjectZig");
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
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    break;
  case ZigValue::Kind::OptionalType:
    if (name == "has_value")
      return 0;
    if (name == "?")
      return 1;
    break;
  case ZigValue::Kind::ErrorUnionType:
    if (name == "error")
      return 0;
    if (name == "value")
      return 1;
    break;
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
    if (name.consume_front("[") && name.consume_back("]") &&
        !name.getAsInteger(10, idx) &&
        idx < llvm::cast<ZigSequenceType>(zig_type)->GetLength())
      return idx;
    break;
  case ZigValue::Kind::FunctionType:
    llvm_unreachable("unimplemented");
  case ZigValue::Kind::PointerType: {
    ZigPointerType *zig_pointer_type = llvm::cast<ZigPointerType>(zig_type);
    switch (zig_pointer_type->GetSize()) {
    case ZigPointerType::Size::One:
    case ZigPointerType::Size::C:
      if (name == "*")
        return 0;
      if (zig_pointer_type->GetSize() != ZigPointerType::Size::C)
        break;
      [[fallthrough]];
    case ZigPointerType::Size::Many:
      if (name.consume_front("[") && name.consume_back("]") &&
          !name.getAsInteger(10, idx))
        return idx;
      break;
    case ZigPointerType::Size::Slice:
      if (name == "ptr")
        return 0;
      if (name == "len")
        return 1;
      break;
    }
    break;
  }
  case ZigValue::Kind::TupleType:
    if ((!name.consume_front("[") || name.consume_back("]")) &&
        !name.getAsInteger(10, idx))
      return idx;
    break;
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType: {
    bool is_tagged = zig_type->GetKind() == ZigValue::Kind::TaggedUnionType;
    llvm::ArrayRef<ZigRecordField> fields =
        llvm::cast<ZigRecordType>(zig_type)->GetFields();
    for (idx = 0; idx < fields.size(); ++idx)
      if (name == fields[idx].GetName())
        return is_tagged + idx;
    if (is_tagged && name == "tag")
      return 0;
    break;
  }
  }
  return UINT32_MAX;
}

size_t TypeSystemZig::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indices) {
  if (uint32_t idx =
          GetIndexOfChildWithName(type, name, omit_empty_base_classes);
      idx == UINT32_MAX) {
    if (ZigPointerType *zig_pointer_type =
            llvm::dyn_cast_if_present<ZigPointerType>(UnwrapType(type)))
      switch (zig_pointer_type->GetSize()) {
      case ZigPointerType::Size::One:
        if (IsAggregateType(zig_pointer_type->GetChildType()->AsOpaqueType()))
          if (uint32_t idx = GetIndexOfChildWithName(
                  zig_pointer_type->GetChildType()->AsOpaqueType(), name,
                  omit_empty_base_classes);
              idx != UINT32_MAX) {
            child_indices.push_back(0);
            child_indices.push_back(idx);
          }
        break;
      case ZigPointerType::Size::Many:
      case ZigPointerType::Size::C:
        break;
      case ZigPointerType::Size::Slice:
        if (name.consume_front("[") && name.consume_back("]") &&
            !name.getAsInteger(10, idx)) {
          child_indices.push_back(0);
          child_indices.push_back(idx);
        }
        break;
      }
  } else
    child_indices.push_back(idx);
  return child_indices.size();
}

ValueObject *TypeSystemZig::GetStringPointer(opaque_compiler_type_t type,
                                             ValueObject *valobj,
                                             uint64_t *length,
                                             char *terminator) {
  if (length)
    *length = UINT64_MAX;
  ZigValue *zig_sentinel = nullptr;
  if (ZigPointerType *zig_type =
          llvm::dyn_cast_if_present<ZigPointerType>(UnwrapType(type)))
    switch (zig_type->GetSize()) {
    case ZigPointerType::Size::One:
      if (ZigArrayType *zig_array_type =
              llvm::dyn_cast<ZigArrayType>(zig_type->GetChildType())) {
        if (length)
          *length = zig_array_type->GetLength();
        zig_sentinel = zig_array_type->GetSentinel();
        break;
      }
      return nullptr;
    case ZigPointerType::Size::Many:
      zig_sentinel = zig_type->GetSentinel();
      break;
    case ZigPointerType::Size::Slice:
      if ((valobj = valobj->GetNonSyntheticValue().get())) {
        zig_sentinel = zig_type->GetSentinel();
        if (length)
          if (ValueObjectSP len_child = valobj->GetChildMemberWithName("len"))
            *length = len_child->GetValueAsUnsigned(UINT64_MAX);
        valobj = valobj->GetChildMemberWithName("ptr").get();
        break;
      }
      return nullptr;
    case ZigPointerType::Size::C:
      break;
    }
  else
    return nullptr;
  if (terminator) {
    if (ZigInt *zig_int_sentinel =
            llvm::dyn_cast_if_present<ZigInt>(zig_sentinel);
        zig_int_sentinel && zig_int_sentinel->GetType()->GetBitSize() == 8)
      *terminator = zig_int_sentinel->GetValue().getZExtValue();
    else
      *terminator = '\0';
  }
  return valobj;
}

CompilerType
TypeSystemZig::GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) {
  if (ZigType *zig_type = UnwrapType(type))
    if (ZigScope *zig_scope = zig_type->GetNamespace())
      if (std::vector<CompilerDecl> decls = DeclContextFindDeclByName(
              zig_scope->AsOpaqueDeclContext(), ConstString(name), false);
          decls.size() == 1)
        if (ZigConstant *zig_nested_const =
                llvm::dyn_cast<ZigConstant>(UnwrapDecl(decls[0])))
          if (ZigType *zig_nested_type = llvm::dyn_cast_if_present<ZigType>(
                  zig_nested_const->GetValue()))
            return WrapType(zig_nested_type);
  return WrapType();
}

lldb::ValueObjectSP
TypeSystemZig::CreateValueFromType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  return ValueObjectZig::Create(exe_scope, UnwrapType(type));
}

static bool IsIdentifierStart(char c) { return llvm::isAlpha(c) || c == '_'; }
static bool IsIdentifierPart(char c) {
  return IsIdentifierStart(c) || llvm::isDigit(c);
}
static bool IsIdentifier(llvm::StringRef id) {
  if (id.empty())
    return false;
  if (!IsIdentifierStart(id[0]))
    return false;
  for (char c : id)
    if (!IsIdentifierPart(c))
      return false;
  return !ZigLexer::IsKeyword(id);
}

void TypeSystemZig::PrintIdentifier(llvm::StringRef id, Stream &s) {
  if (IsIdentifier(id)) {
    s << id;
    return;
  }
  s << "@\"";
  for (char c : id) {
    switch (c) {
    case '\n':
      s << "\\n";
      break;
    case '\r':
      s << "\\r";
      break;
    case '\t':
      s << "\\t";
      break;
    case '\\':
      s << "\\\\";
      break;
    case '\'':
      s << "\\\'";
      break;
    case '\"':
      s << "\\\"";
      break;
    default:
      if (llvm::isPrint(c))
        s << c;
      else {
        s << "\\x";
        s.PutHex8(c);
      }
      break;
    }
  }
  s << '\"';
}

ConstString TypeSystemZig::ChildFieldName(llvm::StringRef name) {
  StreamString s;
  s << '.';
  PrintIdentifier(name, s);
  return ConstString(s.GetString());
}

bool TypeSystemZig::DumpTagValue(ZigTagType *type, Stream &s,
                                 llvm::APSInt value) {
  for (const ZigTagField &field : type->GetFields()) {
    if (field.GetValue()->GetValue() != value)
      continue;
    switch (type->GetKind()) {
    default:
      llvm_unreachable("already checked");
    case ZigValue::Kind::ErrorSetType:
      s << "error";
      break;
    case ZigValue::Kind::GeneratedTagType:
    case ZigValue::Kind::EnumType:
      break;
    }
    s << '.';
    PrintIdentifier(field.GetName(), s);
    return true;
  }
  s << '@';
  switch (type->GetKind()) {
  default:
    llvm_unreachable("already checked");
  case ZigValue::Kind::ErrorSetType:
    s << "error";
    break;
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    s << "enum";
    break;
  }
  s.AsRawOstream() << "FromInt(" << value << ')';
  return true;
}

bool TypeSystemZig::DumpValue(ZigValue *value, Stream &s) {
  if (!value)
    return false;
  switch (value->GetKind()) {
  case ZigValue::Kind::Constant:
  case ZigValue::Kind::Alias: {
    ZigConstant *constant = llvm::cast<ZigConstant>(value);
    s << "const " << constant->GetName() << ": "
      << constant->GetType()->GetQualifiedName() << " = ";
    return DumpValue(constant->GetValue(), s);
  }
  case ZigValue::Kind::Variable: {
    ZigVariable *variable = llvm::cast<ZigVariable>(value);
    s << "var " << variable->GetName() << ": "
      << variable->GetType()->GetQualifiedName();
    return true;
  }
  case ZigValue::Kind::Function:
    s << llvm::cast<ZigFunction>(value)->GetName();
    return true;
  case ZigValue::Kind::Data:
    return false;
  case ZigValue::Kind::OnlyPossibleValue:
    switch (value->GetType()->GetKind()) {
    default:
      return false;
    case ZigValue::Kind::VoidType:
      s << "{}";
      return true;
    case ZigValue::Kind::UndefinedType:
      s << "undefined";
      return true;
    case ZigValue::Kind::NullType:
      s << "null";
      return true;
    }
  case ZigValue::Kind::ComptimeInt:
    s.AsRawOstream() << llvm::cast<ZigComptimeInt>(value)->GetValue();
    return true;
  case ZigValue::Kind::ComptimeFloat: {
    llvm::SmallString<64> str;
    llvm::cast<ZigComptimeFloat>(value)->GetValue().toString(str);
    s << str;
    return true;
  }
  case ZigValue::Kind::EnumLiteral:
    s << '.';
    PrintIdentifier(llvm::cast<ZigEnumLiteral>(value)->GetValue(), s);
    return true;
  case ZigValue::Kind::BoolFalse:
    s << "false";
    return true;
  case ZigValue::Kind::BoolTrue:
    s << "true";
    return true;
  case ZigValue::Kind::Int:
    s.AsRawOstream() << llvm::cast<ZigInt>(value)->GetValue();
    return true;
  case ZigValue::Kind::Float: {
    llvm::SmallString<64> str;
    llvm::cast<ZigFloat>(value)->GetValue().toString(str);
    s << str;
    return true;
  }
  case ZigValue::Kind::Pointer:
  case ZigValue::Kind::Slice:
    s << '&';
    return DumpValue(llvm::cast<ZigPointer>(value)->GetPointee(), s);
  case ZigValue::Kind::Tag: {
    ZigTag *zig_tag = llvm::cast<ZigTag>(value);
    return DumpTagValue(zig_tag->GetType(), s, zig_tag->GetValue()->GetValue());
  }
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    s << llvm::cast<ZigType>(value)->GetQualifiedName();
    return true;
  }
}

bool TypeSystemZig::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                  Format format, const DataExtractor &data,
                                  offset_t byte_offset, size_t byte_size,
                                  uint32_t bitfield_bit_size,
                                  uint32_t bitfield_bit_offset,
                                  ExecutionContextScope *exe_scope) {
  if (!type)
    return false;
  ZigType *zig_type = UnwrapType(type);
  bitfield_bit_size = zig_type->GetBitSize();
  bool is_bitfield = bitfield_bit_offset || bitfield_bit_size % 8 != 0;
  if (is_bitfield)
    byte_size = llvm::divideCeil(bitfield_bit_offset + bitfield_bit_size, 8);
  else
    bitfield_bit_size = 0;
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
  case ZigValue::Kind::ComptimeIntType:
  case ZigValue::Kind::ComptimeFloatType:
  case ZigValue::Kind::EnumLiteralType:
    llvm_unreachable("handled by ValueObjectZig");
  case ZigValue::Kind::VoidType:
    s << "{}";
    return true;
  case ZigValue::Kind::NoReturnType:
  case ZigValue::Kind::AnyOpaqueType:
  case ZigValue::Kind::AnyType:
    llvm_unreachable("valueless");
  case ZigValue::Kind::UndefinedType:
    s << "undefined";
    return true;
  case ZigValue::Kind::NullType:
    s << "null";
    return true;
  case ZigValue::Kind::BoolType:
  case ZigValue::Kind::FloatType:
  case ZigValue::Kind::PointerType:
    break;
  case ZigValue::Kind::FunctionType:
    llvm_unreachable("unimplemented");
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
  case ZigValue::Kind::PackedStructType:
  case ZigValue::Kind::UnionType:
  case ZigValue::Kind::TaggedUnionType:
  case ZigValue::Kind::PackedUnionType:
    return false;
  case ZigValue::Kind::IntType:
    if (llvm::cast<ZigIntType>(zig_type)->GetBitSize())
      break;
    switch (format) {
    default:
      break;
    case eFormatBinary:
      s << "0b";
      break;
    case eFormatHex:
    case eFormatHexUppercase:
      s << "0x";
      break;
    case eFormatOctal:
      s << "0o";
      break;
    }
    s << '0';
    return true;
  case ZigValue::Kind::ErrorSetType:
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType: {
    if (format == eFormatEnum || format == eFormatDefault) {
      if (!data.ValidOffsetForDataOfSize(byte_offset, byte_size))
        return false;
      ZigTagType *tag_type = llvm::cast<ZigTagType>(zig_type);
      return DumpTagValue(
          tag_type, s,
          LoadTargetInt(tag_type->GetBackingType(),
                        data.GetData().slice(byte_offset, byte_size),
                        is_bitfield
                            ? std::optional<uint32_t>(bitfield_bit_offset)
                            : std::nullopt));
    }
    break;
  }
  }
  // We are down to a scalar type that we just need to display.
  uint32_t item_count = 1;
  const llvm::fltSemantics *flt_semantics = nullptr;
  // A few formats, we might need to modify our size and count for
  // depending
  // on how we are trying to display the value...
  switch (format) {
  default:
  case eFormatBoolean:
  case eFormatBinary:
  case eFormatDecimal:
  case eFormatEnum:
  case eFormatHex:
  case eFormatHexUppercase:
  case eFormatOctal:
  case eFormatOSType:
  case eFormatUnsigned:
  case eFormatPointer:
  case eFormatVectorOfChar:
  case eFormatVectorOfSInt8:
  case eFormatVectorOfUInt8:
  case eFormatVectorOfSInt16:
  case eFormatVectorOfUInt16:
  case eFormatVectorOfSInt32:
  case eFormatVectorOfUInt32:
  case eFormatVectorOfSInt64:
  case eFormatVectorOfUInt64:
  case eFormatVectorOfUInt128:
    break;
  case eFormatCString: // NULL terminated C strings
    if (data.GetByteSize() == 0) {
      s << "\"\"";
      return true;
    }
    break;
  case eFormatFloat:
  case eFormatComplex:
  case eFormatVectorOfFloat16:
  case eFormatVectorOfFloat32:
  case eFormatVectorOfFloat64:
    flt_semantics = &ZigFloatType::GetSemantics(UnwrapType(type)->GetBitSize());
    break;
  case eFormatChar:
  case eFormatCharPrintable:
  case eFormatCharArray:
  case eFormatBytes:
  case eFormatUnicode8:
  case eFormatBytesWithASCII:
    item_count = byte_size / 1;
    byte_size = 1;
    bitfield_bit_size = 0;
    bitfield_bit_offset = 0;
    break;
  case eFormatUnicode16:
    item_count = byte_size / 2;
    byte_size = 2;
    bitfield_bit_size = 0;
    bitfield_bit_offset = 0;
    break;
  case eFormatUnicode32:
    item_count = byte_size / 4;
    byte_size = 4;
    bitfield_bit_size = 0;
    bitfield_bit_offset = 0;
    break;
  }
  return DumpDataExtractor(data, &s, byte_offset, format, byte_size, item_count,
                           UINT32_MAX, LLDB_INVALID_ADDRESS, bitfield_bit_size,
                           bitfield_bit_offset, exe_scope, false,
                           flt_semantics);
}

bool TypeSystemZig::DumpTypeDecl(opaque_compiler_type_t type, Stream &s) {
  if (!type)
    return false;
  ZigType *zig_type = UnwrapType(type);
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
  case ZigValue::Kind::OptionalType:
  case ZigValue::Kind::PointerType:
  case ZigValue::Kind::ErrorUnionType:
  case ZigValue::Kind::ArrayType:
  case ZigValue::Kind::VectorType:
  case ZigValue::Kind::FunctionType:
    s << zig_type->GetName();
    return true;
  case ZigValue::Kind::ErrorSetType:
    s << "error";
    return true;
  case ZigValue::Kind::GeneratedTagType:
  case ZigValue::Kind::EnumType:
    s << "enum";
    return true;
  case ZigValue::Kind::TupleType:
  case ZigValue::Kind::StructType:
    s << "struct";
    return true;
  case ZigValue::Kind::PackedStructType:
    s << "packed struct("
      << llvm::cast<ZigPackedStructType>(zig_type)
             ->GetBackingType()
             ->GetQualifiedName()
      << ')';
    return true;
  case ZigValue::Kind::UnionType:
    s << "union";
    return true;
  case ZigValue::Kind::TaggedUnionType:
    s << "union(";
    if (llvm::isa<ZigEnumType>(
            llvm::cast<ZigTaggedUnionType>(zig_type)->GetTagType()))
      s << zig_type->GetQualifiedName();
    else
      s << "enum";
    s << ')';
    return true;
  case ZigValue::Kind::PackedUnionType:
    s << "packed union ("
      << llvm::cast<ZigPackedUnionType>(zig_type)
             ->GetBackingType()
             ->GetQualifiedName()
      << ')';
    return true;
  }
}

void TypeSystemZig::DumpTypeDescription(opaque_compiler_type_t type,
                                        DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);
}

void TypeSystemZig::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {
  ZigType *zig_type = UnwrapType(type);
  if (llvm::isa_and_present<ZigContainer>(zig_type->GetNamespace()))
    s << "const " << zig_type->GetName() << " = ";
  DumpTypeDecl(zig_type->AsOpaqueType(), s);
  if (ZigTagType *zig_tag_type = llvm::dyn_cast<ZigTagType>(zig_type)) {
    s << " {\n";
    for (const ZigTagField &field : zig_tag_type->GetFields()) {
      s << "    " << field.GetName();
      if (llvm::isa<ZigEnumType>(zig_tag_type))
        s.AsRawOstream() << " = " << field.GetValue()->GetValue();
      s << ",\n";
    }
  } else if (ZigTupleType *zig_tuple_type =
                 llvm::dyn_cast<ZigTupleType>(zig_type)) {
    s << " {\n";
    for (const ZigTupleField &field : zig_tuple_type->GetFields()) {
      s << "    ";
      if (field.IsComptime())
        s << "comptime ";
      s << field.GetType()->GetQualifiedName();
      if (ZigValue *field_comptime_value = field.GetComptimeValue()) {
        s << " = ";
        DumpValue(field_comptime_value, s);
      }
      s << ",\n";
    }
  } else if (ZigRecordType *zig_record_type =
                 llvm::dyn_cast<ZigRecordType>(zig_type)) {
    s << " {\n";
    for (const ZigRecordField &field : zig_record_type->GetFields()) {
      s << "    ";
      if (field.IsComptime())
        s << "comptime ";
      s << field.GetName() << ": " << field.GetType()->GetQualifiedName();
      if (ZigValue *field_default_value = field.GetDefaultValue()) {
        s << " = ";
        DumpValue(field_default_value, s);
      }
      s << ",\n";
    }
  }
  if (llvm::isa_and_present<ZigContainer>(zig_type->GetNamespace()))
    s << "};";
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemZig::dump(opaque_compiler_type_t type) const {
  if (!type)
    return;
  llvm_unreachable("unimplemented");
}
#endif

void TypeSystemZig::Dump(llvm::raw_ostream &output) {
  llvm_unreachable("unimplemented");
}

CompilerDeclContext TypeSystemZig::WrapScope(ZigScope *zig_scope) {
  assert(VerifyDeclContext(zig_scope) && "not owned by this");
  return CompilerDeclContext(this, zig_scope);
}
ZigScope *TypeSystemZig::UnwrapScope(void *opaque_decl_ctx) {
  assert(VerifyDeclContext(opaque_decl_ctx) && "not owned by this");
  return static_cast<ZigScope *>(opaque_decl_ctx);
}
ZigScope *TypeSystemZig::UnwrapScope(CompilerDeclContext decl_ctx) {
  assert(decl_ctx.GetTypeSystem() == this && "not owned by this");
  return UnwrapScope(decl_ctx.GetOpaqueDeclContext());
}

CompilerDecl TypeSystemZig::WrapDecl(ZigDeclaration *zig_decl) {
  assert(VerifyDecl(zig_decl) && "not owned by this");
  return CompilerDecl(this, zig_decl);
}
ZigDeclaration *TypeSystemZig::UnwrapDecl(void *opaque_decl) {
  assert(VerifyDecl(opaque_decl) && "not owned by this");
  return static_cast<ZigDeclaration *>(opaque_decl);
}
ZigDeclaration *TypeSystemZig::UnwrapDecl(CompilerDecl decl) {
  assert(decl.GetTypeSystem() == this && "not owned by this");
  return UnwrapDecl(decl.GetOpaqueDecl());
}

CompilerType TypeSystemZig::WrapType(ZigType *zig_type) {
  assert(Verify(zig_type) && "not owned by this");
  return CompilerType(weak_from_this(), zig_type->AsOpaqueType());
}
ZigType *TypeSystemZig::UnwrapType(opaque_compiler_type_t type) {
  assert(Verify(type) && "not owned by this");
  return static_cast<ZigType *>(type);
}
ZigType *TypeSystemZig::UnwrapType(CompilerType type) {
  assert(type.GetTypeSystem() == shared_from_this() && "not owned by this");
  return UnwrapType(type.GetOpaqueQualType());
}

void TypeSystemZig::TypeUpdateParent(opaque_compiler_type_t type) {
  if (!type)
    return;
  SymbolFile *symbol_file = GetSymbolFile();
  if (!symbol_file)
    return;
  if (SymbolFileDWARF *dwarf = llvm::dyn_cast<SymbolFileDWARF>(symbol_file)) {
    auto &map = dwarf->GetForwardDeclCompilerTypeToDIE();
    if (auto it = map.find(type); it != map.end())
      dwarf->GetDecl(dwarf->GetDIE(it->second));
  } else
    symbol_file->ParseDeclsForContext(
        WrapScope(UnwrapType(type)->GetNamespace()));
}
void TypeSystemZig::SetParentScope(opaque_compiler_type_t type,
                                   void *opaque_decl_ctx) {
  assert(opaque_decl_ctx && "missing parent");
  ZigContainer *zig_container =
      llvm::cast<ZigContainer>(UnwrapType(type)->GetNamespace());
  assert(!zig_container->m_parent && "namespace parent already set");
  zig_container->m_parent = UnwrapScope(opaque_decl_ctx);
}
void TypeSystemZig::SetFields(opaque_compiler_type_t type,
                              llvm::ArrayRef<ZigRecordField> fields) {
  ZigRecordType *zig_record_type = llvm::cast<ZigRecordType>(UnwrapType(type));
  assert(!zig_record_type->m_fields.data() && "record fields already set");
  zig_record_type->m_fields = fields.copy(m_allocator);
}

llvm::APSInt TypeSystemZig::LoadTargetInt(ZigType *type,
                                          llvm::ArrayRef<uint8_t> bytes,
                                          std::optional<uint32_t> bit_offset) {
  assert((bit_offset ? llvm::divideCeil(*bit_offset + type->GetBitSize(), 8)
                     : type->GetByteSize()) == bytes.size() &&
         "bytes size mismatch");
  bool is_signed = false;
  if (ZigIntType *int_type = llvm::dyn_cast<ZigIntType>(type))
    is_signed = int_type->IsSigned();
  llvm::APSInt value(UINT64_C(8) * bytes.size(), !is_signed);
  LoadIntFromMemory(value, bytes.data(), bytes.size());
  if (GetByteOrder() != endian::InlHostByteOrder())
    value = value.byteSwap();
  value.lshrInPlace(bit_offset.value_or(0));
  return value.trunc(type->GetBitSize());
}

char ScratchTypeSystemZig::ID;

ScratchTypeSystemZig::ScratchTypeSystemZig(Target &target)
    : TypeSystemZig("scratch ASTContext", target.GetArchitecture()),
      m_target_wp(target.weak_from_this()) {}

UserExpression *ScratchTypeSystemZig::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  if (TargetSP target_sp = m_target_wp.lock())
    return new ZigUserExpression(*target_sp, expr, prefix, language,
                                 desired_type, options, ctx_obj);
  return nullptr;
}

void ScratchTypeSystemZig::Dump(llvm::raw_ostream &output) {
  llvm_unreachable("unimplemented");
}
