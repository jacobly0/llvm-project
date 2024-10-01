//===-- ZigLexer.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGLEXER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGLEXER_H

#include "llvm/ADT/StringRef.h"

namespace lldb_private {

class ZigLexer {
public:
  struct Token {
    enum class Kind : uint8_t {
      Error,
      End,
      Whitespace,
      Exclamation,
      ExclamationEqual,
      StringLiteral,
      Percent,
      PercentEqual,
      Ampersand,
      AmpersandEqual,
      CharLiteral,
      LeftParen,
      RightParen,
      Asterisk,
      AsteriskPercent,
      AsteriskPercentEqual,
      AsteriskAsterisk,
      AsteriskEqual,
      AsteriskPipe,
      AsteriskPipeEqual,
      Plus,
      PlusPercent,
      PlusPercentEqual,
      PlusPlus,
      PlusEqual,
      PlusPipe,
      PlusPipeEqual,
      Comma,
      Minus,
      MinusPercent,
      MinusPercentEqual,
      MinusEqual,
      MinusRightArrow,
      MinusPipe,
      MinusPipeEqual,
      Dot,
      DotAsterisk,
      DotDot,
      DotDotDot,
      DotQuestion,
      Slash,
      Comment,
      DocComment,
      SlashEqual,
      NumberLiteral,
      Colon,
      Semicolon,
      LeftArrow,
      LeftArrowLeftArrow,
      LeftArrowLeftArrowEqual,
      LeftArrowLeftArrowPipe,
      LeftArrowLeftArrowPipeEqual,
      LeftArrowEqual,
      Equal,
      EqualEqual,
      EqualRightArrow,
      RightArrow,
      RightArrowEqual,
      RightArrowRightArrow,
      RightArrowRightArrowEqual,
      Question,
      Builtin,
      QuotedIdentifier,
      LeftBracket,
      MultilineStringLiteral,
      RightBracket,
      Caret,
      CaretEqual,
      LeftBrace,
      Pipe,
      PipeEqual,
      PipePipe,
      RightBrace,
      Tilde,
      Keyword,
      Primitive,
      Identifier,
    };

    Kind kind;
    llvm::StringRef source;
    llvm::StringRef parsed;
  };

  static bool IsKeyword(llvm::StringRef identifier);
  static bool IsPrimitive(llvm::StringRef identifier);

  ZigLexer(const char *source) : m_keep_whitespace(false), m_pos(source) {}

  void KeepWhitespace() { m_keep_whitespace = true; }

  Token NextToken();

private:
  Token NewToken(Token::Kind kind, const char *start,
                 llvm::StringRef parsed = llvm::StringRef());
  bool ParseStringLiteral();

  bool m_keep_whitespace;
  const char *m_pos;
  std::string m_buf;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_ZIG_ZIGLEXER_H
