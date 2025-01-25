//===-- ZigLanguage.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_ZIG_ZIGLANGUAGE_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_ZIG_ZIGLANGUAGE_H

#include "ZigHighlighter.h"

#include "lldb/Target/Language.h"

namespace lldb_private {

class ZigLanguage : public Language {
  ZigHighlighter m_highlighter;

public:
  ZigLanguage() = default;

  ~ZigLanguage() override = default;

  static lldb::LanguageType GetLanguageTypeStatic() {
    return lldb::eLanguageTypeZig;
  }

  lldb::LanguageType GetLanguageType() const override {
    return GetLanguageTypeStatic();
  }

  llvm::StringRef GetUserEntryPointName() const override { return "main"; }

  std::unique_ptr<TypeScavenger> GetTypeScavenger() override;
  lldb::TypeCategoryImplSP GetFormatters() override;

  HardcodedFormatters::HardcodedSummaryFinder GetHardcodedSummaries() override;

  HardcodedFormatters::HardcodedSyntheticFinder
  GetHardcodedSynthetics() override;

  LazyBool IsLogicalTrue(ValueObject &valobj, Status &error) override;

  bool IsNilReference(ValueObject &valobj) override;

  llvm::StringRef GetNilReferenceSummaryString() override { return "null"; }

  bool IsSourceFile(llvm::StringRef file_path) const override {
    return file_path.ends_with(".zig");
  }

  const Highlighter *GetHighlighter() const override { return &m_highlighter; }

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb_private::Language *CreateInstance(lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() { return "zig"; }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_ZIG_ZIGLANGUAGE_H
