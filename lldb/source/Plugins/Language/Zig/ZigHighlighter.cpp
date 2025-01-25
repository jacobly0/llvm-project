//===-- ZigHighlighter.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigHighlighter.h"

#include "ZigLexer.h"

#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

void ZigHighlighter::Highlight(const HighlightStyle &options,
                               llvm::StringRef line,
                               std::optional<size_t> cursor_pos,
                               llvm::StringRef previous_lines,
                               Stream &result) const {
  std::string line_str = line.str();
  ZigLexer lex(line_str.c_str());
  lex.KeepWhitespace();
  while (true) {
    ZigLexer::Token tok = lex.NextToken();
    StreamString selected;
    if (cursor_pos && &line_str[*cursor_pos] >= tok.source.begin() &&
        &line_str[*cursor_pos] < tok.source.end()) {
      options.selected.Apply(selected, tok.source);
      tok.source = selected.GetString();
    }
    HighlightStyle::ColorStyle color;
    switch (tok.kind) {
    case ZigLexer::Token::Kind::Error:
      result << llvm::StringRef(line_str).substr(tok.source.begin() -
                                                 line_str.data());
      return;
    case ZigLexer::Token::Kind::Whitespace:
      break;
    case ZigLexer::Token::Kind::End:
      return;
    case ZigLexer::Token::Kind::Exclamation:
    case ZigLexer::Token::Kind::ExclamationEqual:
    case ZigLexer::Token::Kind::Percent:
    case ZigLexer::Token::Kind::PercentEqual:
    case ZigLexer::Token::Kind::Ampersand:
    case ZigLexer::Token::Kind::AmpersandEqual:
    case ZigLexer::Token::Kind::Asterisk:
    case ZigLexer::Token::Kind::AsteriskPercent:
    case ZigLexer::Token::Kind::AsteriskPercentEqual:
    case ZigLexer::Token::Kind::AsteriskAsterisk:
    case ZigLexer::Token::Kind::AsteriskEqual:
    case ZigLexer::Token::Kind::AsteriskPipe:
    case ZigLexer::Token::Kind::AsteriskPipeEqual:
    case ZigLexer::Token::Kind::Plus:
    case ZigLexer::Token::Kind::PlusPercent:
    case ZigLexer::Token::Kind::PlusPercentEqual:
    case ZigLexer::Token::Kind::PlusPlus:
    case ZigLexer::Token::Kind::PlusEqual:
    case ZigLexer::Token::Kind::PlusPipe:
    case ZigLexer::Token::Kind::PlusPipeEqual:
    case ZigLexer::Token::Kind::Minus:
    case ZigLexer::Token::Kind::MinusPercent:
    case ZigLexer::Token::Kind::MinusPercentEqual:
    case ZigLexer::Token::Kind::MinusEqual:
    case ZigLexer::Token::Kind::MinusRightArrow:
    case ZigLexer::Token::Kind::MinusPipe:
    case ZigLexer::Token::Kind::MinusPipeEqual:
    case ZigLexer::Token::Kind::Dot:
    case ZigLexer::Token::Kind::DotAsterisk:
    case ZigLexer::Token::Kind::DotDot:
    case ZigLexer::Token::Kind::DotDotDot:
    case ZigLexer::Token::Kind::DotQuestion:
    case ZigLexer::Token::Kind::Slash:
    case ZigLexer::Token::Kind::SlashEqual:
    case ZigLexer::Token::Kind::LeftArrow:
    case ZigLexer::Token::Kind::LeftArrowLeftArrow:
    case ZigLexer::Token::Kind::LeftArrowLeftArrowEqual:
    case ZigLexer::Token::Kind::LeftArrowLeftArrowPipe:
    case ZigLexer::Token::Kind::LeftArrowLeftArrowPipeEqual:
    case ZigLexer::Token::Kind::LeftArrowEqual:
    case ZigLexer::Token::Kind::Equal:
    case ZigLexer::Token::Kind::EqualEqual:
    case ZigLexer::Token::Kind::EqualRightArrow:
    case ZigLexer::Token::Kind::RightArrow:
    case ZigLexer::Token::Kind::RightArrowEqual:
    case ZigLexer::Token::Kind::RightArrowRightArrow:
    case ZigLexer::Token::Kind::RightArrowRightArrowEqual:
    case ZigLexer::Token::Kind::Question:
    case ZigLexer::Token::Kind::Caret:
    case ZigLexer::Token::Kind::CaretEqual:
    case ZigLexer::Token::Kind::Pipe:
    case ZigLexer::Token::Kind::PipeEqual:
    case ZigLexer::Token::Kind::PipePipe:
    case ZigLexer::Token::Kind::Tilde:
      color = options.operators;
      break;
    case ZigLexer::Token::Kind::StringLiteral:
    case ZigLexer::Token::Kind::MultilineStringLiteral:
    case ZigLexer::Token::Kind::CharLiteral:
      color = options.string_literal;
      break;
    case ZigLexer::Token::Kind::LeftParen:
    case ZigLexer::Token::Kind::RightParen:
      color = options.parentheses;
      break;
    case ZigLexer::Token::Kind::NumberLiteral:
      color = options.scalar_literal;
      break;
    case ZigLexer::Token::Kind::Colon:
      color = options.colon;
      break;
    case ZigLexer::Token::Kind::Semicolon:
      color = options.semicolons;
      break;
    case ZigLexer::Token::Kind::Builtin:
      color = options.pp_directive;
      break;
    case ZigLexer::Token::Kind::LeftBracket:
    case ZigLexer::Token::Kind::RightBracket:
      color = options.square_brackets;
      break;
    case ZigLexer::Token::Kind::LeftBrace:
    case ZigLexer::Token::Kind::RightBrace:
      color = options.braces;
      break;
    case ZigLexer::Token::Kind::Comma:
      color = options.comma;
      break;
    case ZigLexer::Token::Kind::Comment:
    case ZigLexer::Token::Kind::DocComment:
      color = options.comment;
      break;
    case ZigLexer::Token::Kind::QuotedIdentifier:
    case ZigLexer::Token::Kind::Identifier:
      color = options.identifier;
      break;
    case ZigLexer::Token::Kind::Keyword:
    case ZigLexer::Token::Kind::Primitive:
      color = options.keyword;
      break;
    }
    color.Apply(result, tok.source);
  }
}
