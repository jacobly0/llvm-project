//===-- ZigLexer.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigLexer.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lldb_private;

static bool IsIdentifierStart(char c) { return llvm::isAlnum(c) || c == '_'; }
static bool IsNumberStart(char c) { return llvm::isDigit(c); }
static bool IsIdentifierPart(char c) {
  return IsIdentifierStart(c) || llvm::isDigit(c);
}
static bool IsNumberPart(char c) { return IsIdentifierPart(c) || c == '.'; }
static bool IsIntegerType(llvm::StringRef identifier) {
  if (identifier.empty())
    return false;
  if (identifier[0] != 'i' && identifier[0] != 'u')
    return false;
  uint16_t bits;
  if (identifier.substr(1).getAsInteger(10, bits))
    return false;
  return bits ? identifier[1] != '0' : identifier.size() == 2;
}

bool ZigLexer::IsKeyword(llvm::StringRef identifier) {
  return llvm::StringSwitch<bool>(identifier)
      .Case("addrspace", true)
      .Case("align", true)
      .Case("allowzero", true)
      .Case("and", true)
      .Case("anyframe", true)
      .Case("anytype", true)
      .Case("asm", true)
      .Case("async", true)
      .Case("await", true)
      .Case("break", true)
      .Case("callconv", true)
      .Case("catch", true)
      .Case("comptime", true)
      .Case("const", true)
      .Case("continue", true)
      .Case("defer", true)
      .Case("else", true)
      .Case("enum", true)
      .Case("errdefer", true)
      .Case("error", true)
      .Case("export", true)
      .Case("extern", true)
      .Case("fn", true)
      .Case("for", true)
      .Case("if", true)
      .Case("inline", true)
      .Case("noalias", true)
      .Case("noinline", true)
      .Case("nosuspend", true)
      .Case("opaque", true)
      .Case("or", true)
      .Case("orelse", true)
      .Case("packed", true)
      .Case("pub", true)
      .Case("resume", true)
      .Case("return", true)
      .Case("linksection", true)
      .Case("struct", true)
      .Case("suspend", true)
      .Case("switch", true)
      .Case("test", true)
      .Case("threadlocal", true)
      .Case("try", true)
      .Case("union", true)
      .Case("unreachable", true)
      .Case("usingnamespace", true)
      .Case("var", true)
      .Case("volatile", true)
      .Case("while", true)
      .Default(false);
}
bool ZigLexer::IsPrimitive(llvm::StringRef identifier) {
  return llvm::StringSwitch<bool>(identifier)
      .Case("_", true)
      .Case("anyerror", true)
      .Case("anyframe", true)
      .Case("anyopaque", true)
      .Case("bool", true)
      .Case("c_int", true)
      .Case("c_long", true)
      .Case("c_longdouble", true)
      .Case("c_longlong", true)
      .Case("c_char", true)
      .Case("c_short", true)
      .Case("c_uint", true)
      .Case("c_ulong", true)
      .Case("c_ulonglong", true)
      .Case("c_ushort", true)
      .Case("comptime_float", true)
      .Case("comptime_int", true)
      .Case("f128", true)
      .Case("f16", true)
      .Case("f32", true)
      .Case("f64", true)
      .Case("f80", true)
      .Case("false", true)
      .Case("isize", true)
      .Case("noreturn", true)
      .Case("null", true)
      .Case("true", true)
      .Case("type", true)
      .Case("undefined", true)
      .Case("usize", true)
      .Case("void", true)
      .Default(IsIntegerType(identifier));
}

ZigLexer::Token ZigLexer::NextToken() {
  while (true) {
    const char *start = m_pos;
    switch (*m_pos) {
    case 0:
      return NewToken(Token::Kind::End, start);
    case '\t':
    case '\n':
    case '\r':
    case ' ':
      ++m_pos;
      while (*m_pos == '\t' || *m_pos == '\n' || *m_pos == '\r' ||
             *m_pos == ' ')
        ++m_pos;
      if (!m_keep_whitespace)
        continue;
      return NewToken(Token::Kind::Whitespace, start);
    case '!':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Exclamation, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::ExclamationEqual, start);
      }
    case '"':
      if (!ParseStringLiteral())
        return NewToken(Token::Kind::Error, start);
      m_buf.push_back(0);
      return NewToken(Token::Kind::StringLiteral, start, m_buf);
    case '%':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Percent, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::PercentEqual, start);
      }
    case '&':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Ampersand, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::AmpersandEqual, start);
      }
    case '\'':
      if (!ParseStringLiteral())
        return NewToken(Token::Kind::Error, start);
      return NewToken(Token::Kind::CharLiteral, start, m_buf);
    case '(':
      ++m_pos;
      return NewToken(Token::Kind::LeftParen, start);
    case ')':
      ++m_pos;
      return NewToken(Token::Kind::RightParen, start);
    case '*':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Asterisk, start);
      case '%':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::AsteriskPercent, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::AsteriskPercentEqual, start);
        }
      case '*':
        ++m_pos;
        return NewToken(Token::Kind::AsteriskAsterisk, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::AsteriskEqual, start);
      case '|':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::AsteriskPipe, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::AsteriskPipeEqual, start);
        }
      }
    case '+':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Plus, start);
      case '%':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::PlusPercent, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::PlusPercentEqual, start);
        }
      case '+':
        ++m_pos;
        return NewToken(Token::Kind::PlusPlus, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::PlusEqual, start);
      case '|':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::PlusPipe, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::PlusPipeEqual, start);
        }
      }
    case ',':
      ++m_pos;
      return NewToken(Token::Kind::Comma, start);
    case '-':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Minus, start);
      case '%':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::MinusPercent, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::MinusPercentEqual, start);
        }
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::MinusEqual, start);
      case '>':
        ++m_pos;
        return NewToken(Token::Kind::MinusRightArrow, start);
      case '|':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::MinusPipe, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::MinusPipeEqual, start);
        }
      }
    case '.':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Dot, start);
      case '*':
        ++m_pos;
        return NewToken(Token::Kind::DotAsterisk, start);
      case '.':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::DotDot, start);
        case '.':
          ++m_pos;
          return NewToken(Token::Kind::DotDotDot, start);
        }
      case '?':
        ++m_pos;
        return NewToken(Token::Kind::DotQuestion, start);
      }
    case '/':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Slash, start);
      case '/':
        switch (*++m_pos) {
        default:
          while (*m_pos != '\n' && *m_pos != 0)
            ++m_pos;
          return NewToken(Token::Kind::Comment, start);
        case '/':
          ++m_pos;
          while (*m_pos != '\n' && *m_pos != 0)
            ++m_pos;
          return NewToken(Token::Kind::Comment, start);
        }
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::SlashEqual, start);
      }
    case ':':
      ++m_pos;
      return NewToken(Token::Kind::Colon, start);
    case ';':
      ++m_pos;
      return NewToken(Token::Kind::Semicolon, start);
    case '<':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::LeftArrow, start);
      case '<':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::LeftArrowLeftArrow, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::LeftArrowLeftArrowEqual, start);
        case '|':
          switch (*++m_pos) {
          default:
            return NewToken(Token::Kind::LeftArrowLeftArrowPipe, start);
          case '=':
            ++m_pos;
            return NewToken(Token::Kind::LeftArrowLeftArrowPipeEqual, start);
          }
        }
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::LeftArrowEqual, start);
      }
    case '=':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Equal, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::EqualEqual, start);
      case '>':
        ++m_pos;
        return NewToken(Token::Kind::EqualRightArrow, start);
      }
    case '>':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::RightArrow, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::RightArrowEqual, start);
      case '>':
        switch (*++m_pos) {
        default:
          return NewToken(Token::Kind::RightArrowRightArrow, start);
        case '=':
          ++m_pos;
          return NewToken(Token::Kind::RightArrowRightArrowEqual, start);
        }
      }
    case '?':
      ++m_pos;
      return NewToken(Token::Kind::Question, start);
    case '@':
      switch (*++m_pos) {
      default:
        if (!IsIdentifierStart(*++m_pos))
          return NewToken(Token::Kind::Error, start);
        while (IsIdentifierPart(*++m_pos))
          ;
        return NewToken(Token::Kind::Builtin, start, m_buf);
      case '"':
        if (!ParseStringLiteral())
          return NewToken(Token::Kind::Error, start);
        return NewToken(Token::Kind::QuotedIdentifier, start, m_buf);
      }
    case '[':
      ++m_pos;
      return NewToken(Token::Kind::LeftBracket, start);
    case '\\':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Error, start);
      case '\\':
        while (*++m_pos != '\n')
          if (*m_pos == 0)
            return NewToken(Token::Kind::Error, start);
        return NewToken(Token::Kind::MultilineStringLiteral, start,
                        llvm::StringRef(start, m_pos - start + 1).substr(2));
      }
    case ']':
      ++m_pos;
      return NewToken(Token::Kind::RightBracket, start);
    case '^':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Caret, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::CaretEqual, start);
      }
    case '{':
      ++m_pos;
      return NewToken(Token::Kind::LeftBrace, start);
    case '|':
      switch (*++m_pos) {
      default:
        return NewToken(Token::Kind::Pipe, start);
      case '=':
        ++m_pos;
        return NewToken(Token::Kind::PipeEqual, start);
      case '|':
        ++m_pos;
        return NewToken(Token::Kind::PipePipe, start);
      }
    case '}':
      ++m_pos;
      return NewToken(Token::Kind::RightBrace, start);
    case '~':
      ++m_pos;
      return NewToken(Token::Kind::Tilde, start);
    default: {
      if (IsNumberStart(*m_pos)) {
        bool found_dot = false;
        for (m_buf.clear(); IsNumberPart(*m_pos); ++m_pos) {
          if (*m_pos == '_' || *m_pos == '.') {
            if (*m_pos == '.') {
              if (found_dot)
                break;
              found_dot = true;
            }
            if (!llvm::isHexDigit(m_pos[-1]) || !llvm::isHexDigit(m_pos[1]))
              break;
          }
          m_buf.push_back(*m_pos);
        }
        return NewToken(Token::Kind::NumberLiteral, start, m_buf);
      }
      if (!IsIdentifierStart(*m_pos))
        return NewToken(Token::Kind::Error, start);
      while (IsIdentifierPart(*++m_pos))
        ;
      llvm::StringRef identifier(start, m_pos - start);
      return NewToken(IsKeyword(identifier)     ? Token::Kind::Keyword
                      : IsPrimitive(identifier) ? Token::Kind::Primitive
                                                : Token::Kind::Identifier,
                      start, identifier);
    }
    }
  }
}

ZigLexer::Token ZigLexer::NewToken(Token::Kind kind, const char *start,
                                   llvm::StringRef parsed) {
  Token tok;
  tok.kind = kind;
  tok.source = llvm::StringRef(start, m_pos - start);
  tok.parsed = parsed;
  return tok;
}

bool ZigLexer::ParseStringLiteral() {
  m_buf.clear();
  char quote = *m_pos;
  while (*++m_pos != quote)
    switch (*m_pos) {
    case 0:
    case '\n':
    case '\r':
      return false;
    case '\\':
      switch (*++m_pos) {
      case 'n':
        m_buf.push_back('\n');
        break;
      case 'r':
        m_buf.push_back('\r');
        break;
      case 't':
        m_buf.push_back('\t');
        break;
      case '\\':
      case '\'':
      case '"':
        m_buf.push_back(*m_pos);
        break;
      case 'x': {
        uint8_t byte = 0;
        if (!m_pos[1] || !llvm::tryGetHexFromNibbles(m_pos[1], m_pos[2], byte))
          return false;
        m_buf.push_back(byte);
        m_pos += 2;
        break;
      }
      case 'u':
        if (*++m_pos != '{')
          return false;
        const char *start = ++m_pos;
        while (llvm::isHexDigit(*m_pos++))
          ;
        if (*m_pos != '}')
          return false;
        uint32_t codepoint;
        if (llvm::StringRef(start, m_pos - start).getAsInteger(16, codepoint))
          return false;
        if (codepoint <= 0x7f) {
          m_buf.push_back(0x00 | (codepoint >> 0 & 0x7f));
        } else if (codepoint <= 0x7ff) {
          m_buf.push_back(0xc0 | (codepoint >> 6 & 0x1f));
          m_buf.push_back(0x80 | (codepoint >> 0 & 0x3f));
        } else if (codepoint <= 0xffff) {
          m_buf.push_back(0xe0 | (codepoint >> 12 & 0x0f));
          m_buf.push_back(0x80 | (codepoint >> 6 & 0x3f));
          m_buf.push_back(0x80 | (codepoint >> 0 & 0x3f));
        } else if (codepoint <= 0x10ffff) {
          m_buf.push_back(0xf0 | (codepoint >> 18 & 0x07));
          m_buf.push_back(0x80 | (codepoint >> 12 & 0x3f));
          m_buf.push_back(0x80 | (codepoint >> 6 & 0x3f));
          m_buf.push_back(0x80 | (codepoint >> 0 & 0x3f));
        } else
          return false;
        return true;
      }
      break;
    default:
      m_buf.push_back(*m_pos);
      break;
    }
  ++m_pos;
  return true;
}
