//===-- ZigHighlighter.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGE_ZIG_ZIGHIGHLIGHTER_H
#define LLDB_SOURCE_PLUGINS_LANGUAGE_ZIG_ZIGHIGHLIGHTER_H

#include "lldb/Core/Highlighter.h"

namespace lldb_private {

class ZigHighlighter : public Highlighter {
public:
  ZigHighlighter() = default;
  llvm::StringRef GetName() const override { return "zig"; }

  void Highlight(const HighlightStyle &options, llvm::StringRef line,
                 std::optional<size_t> cursor_pos,
                 llvm::StringRef previous_lines, Stream &s) const override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGE_CLANGCOMMON_CLANGHIGHLIGHTER_H
