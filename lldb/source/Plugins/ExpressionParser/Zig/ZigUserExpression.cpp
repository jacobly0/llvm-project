//===-- ZigUserExpression.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ZigUserExpression.h"

#include "ZigExpressionVariable.h"

#include "Plugins/Language/Zig/ZigLexer.h"
#include "Plugins/TypeSystem/Zig/TypeSystemZig.h"
#include "Plugins/TypeSystem/Zig/ValueObjectZig.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"
#include "lldb/ValueObject/ValueObjectVariable.h"

#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <utility>

using namespace lldb;
using namespace lldb_private;

namespace {

class ResultLoc {
public:
  enum Kind : uint8_t { None, Skip, Type };

  explicit ResultLoc() : ResultLoc(Kind::None) {}

  ResultLoc(const ResultLoc &) = delete;
  ResultLoc &operator=(const ResultLoc &) = delete;

  Kind GetKind() const { return m_kind; }
  void Use() { m_used = true; }
  bool IsUsed() const { return m_used; }

protected:
  ResultLoc(Kind kind) : m_kind(kind), m_used(false) {}

private:
  const Kind m_kind;
  bool m_used;
};

class ResultLocSkip : public ResultLoc {
public:
  static bool classof(const ResultLoc *rl) {
    return rl->GetKind() == Kind::Skip;
  }

  explicit ResultLocSkip() : ResultLoc(Kind::Skip) {}
};

class ResultLocType : public ResultLoc {
public:
  static bool classof(const ResultLoc *rl) {
    return rl->GetKind() == Kind::Type;
  }

  explicit ResultLocType(CompilerType type)
      : ResultLoc(Kind::Type), m_type(type) {}

  CompilerType GetType() const { return m_type; }

private:
  CompilerType m_type;
};

class ZigParser {
  using Token = ZigLexer::Token;
  using TK = Token::Kind;

public:
  ZigParser(ExecutionContext &exe_ctx, ValueObject *ctx_obj, const char *source)
      : m_exe_ctx(exe_ctx), m_ctx_obj(ctx_obj), m_lexer(source), m_skip(false) {
    NextToken();
  }

  ValueObjectSP RootExpr() {
    ValueObjectSP result = Expr();
    if (!result || result->GetError().Fail())
      return result;
    if (m_token.kind != TK::End)
      return nullptr;
    return result;
  }

  ValueObjectSP Expr(ResultLoc &&rl = ResultLoc()) {
    return BoolOrExpr(std::move(rl));
  }

  ValueObjectSP BoolOrExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = BoolAndExpr(std::move(rl));
    if (!lhs || lhs->GetError().Fail())
      return lhs;
    while (ConsumeKeyword("or")) {
      if (rl.IsUsed())
        return nullptr;
      CompilerType lhs_type = lhs->GetCompilerType();
      auto lhs_type_system =
          lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!lhs_type_system ||
          !llvm::isa<ZigBoolType>(lhs_type_system->UnwrapType(lhs_type)))
        return nullptr;
      auto lhs_val = lhs->GetValueAsBool();
      if (!lhs_val)
        return CreateValueObject(lhs_val.takeError());
      NextToken();
      bool old_skip = m_skip;
      if (*lhs_val)
        m_skip = true;
      ValueObjectSP rhs = BoolAndExpr(
          ResultLocType(lhs_type_system->GetBasicTypeFromAST(eBasicTypeBool)));
      m_skip = old_skip;
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType rhs_type = rhs->GetCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!rhs_type_system ||
          !llvm::isa<ZigBoolType>(rhs_type_system->UnwrapType(rhs_type)))
        return nullptr;
      if (!*lhs_val)
        lhs.swap(rhs);
    }
    return lhs;
  }

  ValueObjectSP BoolAndExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = CompareExpr(std::move(rl));
    if (!lhs || lhs->GetError().Fail())
      return lhs;
    while (ConsumeKeyword("and")) {
      if (rl.IsUsed())
        return nullptr;
      CompilerType lhs_type = lhs->GetCompilerType();
      auto lhs_type_system =
          lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!lhs_type_system ||
          !llvm::isa<ZigBoolType>(lhs_type_system->UnwrapType(lhs_type)))
        return nullptr;
      auto lhs_val = lhs->GetValueAsBool();
      if (!lhs_val)
        return CreateValueObject(lhs_val.takeError());
      NextToken();
      bool old_skip = m_skip;
      if (!*lhs_val)
        m_skip = true;
      ValueObjectSP rhs = CompareExpr(
          ResultLocType(lhs_type_system->GetBasicTypeFromAST(eBasicTypeBool)));
      m_skip = old_skip;
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType rhs_type = rhs->GetCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!rhs_type_system ||
          !llvm::isa<ZigBoolType>(rhs_type_system->UnwrapType(rhs_type)))
        return nullptr;
      if (*lhs_val)
        lhs.swap(rhs);
    }
    return lhs;
  }

  ValueObjectSP CompareExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = BitwiseExpr(std::move(rl));
    if (!lhs || lhs->GetError().Fail())
      return lhs;
    switch (Token op = m_token; op.kind) {
    case TK::EqualEqual:
    case TK::ExclamationEqual:
    case TK::LeftArrow:
    case TK::RightArrow:
    case TK::LeftArrowEqual:
    case TK::RightArrowEqual: {
      if (rl.IsUsed())
        return nullptr;
      NextToken();
      ValueObjectSP rhs = BitwiseExpr();
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType lhs_type = lhs->GetCompilerType();
      auto lhs_type_system =
          lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      CompilerType rhs_type = rhs->GetCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!lhs_type_system || !rhs_type_system)
        return nullptr;
      ZigType *lhs_zig_type = lhs_type_system->UnwrapType(lhs_type);
      ZigType *rhs_zig_type = rhs_type_system->UnwrapType(rhs_type);
      if (!lhs_zig_type || !rhs_zig_type)
        return nullptr;
      switch (lhs_zig_type->GetKind()) {
      default:
        break;
      case ZigValue::Kind::ComptimeIntType:
      case ZigValue::Kind::IntType: {
        auto lhs_val = lhs->GetValueAsAPSInt();
        if (!lhs_val)
          return CreateValueObject(lhs_val.takeError());
        switch (rhs_zig_type->GetKind()) {
        default:
          break;
        case ZigValue::Kind::ComptimeIntType:
        case ZigValue::Kind::IntType: {
          auto rhs_val = rhs->GetValueAsAPSInt();
          if (!rhs_val)
            return CreateValueObject(rhs_val.takeError());
          int compare = llvm::APSInt::compareValues(*lhs_val, *rhs_val);
          bool result;
          switch (op.kind) {
          default:
            llvm_unreachable("already checked");
          case TK::EqualEqual:
            result = compare == 0;
            break;
          case TK::ExclamationEqual:
            result = compare != 0;
            break;
          case TK::LeftArrow:
            result = compare < 0;
            break;
          case TK::RightArrow:
            result = compare > 0;
            break;
          case TK::LeftArrowEqual:
            result = compare <= 0;
            break;
          case TK::RightArrowEqual:
            result = compare >= 0;
            break;
          }
          return CreateValueObject(lhs_type_system->GetBool(result));
        }
        case ZigValue::Kind::ComptimeFloatType: {
          auto lhs_val = lhs->GetValueAsAPFloat();
          if (!lhs_val)
            return CreateValueObject(lhs_val.takeError());
          switch (rhs_zig_type->GetKind()) {
          default:
            break;
          case ZigValue::Kind::ComptimeFloatType: {
            auto rhs_val = rhs->GetValueAsAPFloat();
            if (!rhs_val)
              return CreateValueObject(rhs_val.takeError());
            bool result;
            switch (op.kind) {
            default:
              llvm_unreachable("already checked");
            case TK::EqualEqual:
              result = *lhs_val == *rhs_val;
              break;
            case TK::ExclamationEqual:
              result = *lhs_val != *rhs_val;
              break;
            case TK::LeftArrow:
              result = *lhs_val < *rhs_val;
              break;
            case TK::RightArrow:
              result = *lhs_val > *rhs_val;
              break;
            case TK::LeftArrowEqual:
              result = *lhs_val <= *rhs_val;
              break;
            case TK::RightArrowEqual:
              result = *lhs_val >= *rhs_val;
              break;
            }
            return CreateValueObject(lhs_type_system->GetBool(result));
          }
          }
          break;
        }
        }
        break;
      }
      }
      return CreateValueObject(Status::FromErrorStringWithFormatv(
          "unimplemented {0} {1} {2}", lhs_type.GetDisplayTypeName(), op.source,
          rhs_type.GetDisplayTypeName()));
    }
    default:
      return lhs;
    }
  }

  ValueObjectSP BitwiseExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = BitShiftExpr(std::move(rl));
    while (true) {
      if (!lhs || lhs->GetError().Fail())
        return lhs;
      switch (Token op = m_token; op.kind) {
      case TK::Ampersand:
      case TK::Caret:
      case TK::Pipe: {
        if (rl.IsUsed())
          return nullptr;
        NextToken();
        ValueObjectSP rhs = BitShiftExpr();
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        lhs = BinaryOp(lhs, op, rhs);
        break;
      }
      case TK::Keyword:
        if (m_token.source == "orelse") {
          if (rl.IsUsed())
            return nullptr;
          NextToken();
          ValueObjectSP rhs = BitShiftExpr();
          if (!rhs || rhs->GetError().Fail())
            return rhs;
          llvm_unreachable("unimplemented");
        }
        if (m_token.source == "catch") {
          if (rl.IsUsed())
            return nullptr;
          NextToken();
          ValueObjectSP rhs = BitShiftExpr();
          if (!rhs || rhs->GetError().Fail())
            return rhs;
          llvm_unreachable("unimplemented");
        }
        [[fallthrough]];
      default:
        return lhs;
      }
    }
  }

  ValueObjectSP BitShiftExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = AdditionExpr(std::move(rl));
    while (true) {
      if (!lhs || lhs->GetError().Fail())
        return lhs;
      switch (Token op = m_token; op.kind) {
      case TK::LeftArrowLeftArrow:
      case TK::RightArrowRightArrow:
      case TK::LeftArrowLeftArrowPipe: {
        if (rl.IsUsed())
          return nullptr;
        NextToken();
        ValueObjectSP rhs = AdditionExpr();
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        lhs = BinaryOp(lhs, op, rhs);
        break;
      }
      default:
        return lhs;
      }
    }
  }

  ValueObjectSP AdditionExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = MultiplyExpr(std::move(rl));
    while (true) {
      if (!lhs || lhs->GetError().Fail())
        return lhs;
      switch (Token op = m_token; op.kind) {
      case TK::Plus:
      case TK::Minus:
      case TK::PlusPlus:
      case TK::PlusPercent:
      case TK::MinusPercent:
      case TK::PlusPipe:
      case TK::MinusPipe: {
        if (rl.IsUsed())
          return nullptr;
        NextToken();
        ValueObjectSP rhs = MultiplyExpr();
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        lhs = BinaryOp(lhs, op, rhs);
        break;
      }
      default:
        return lhs;
      }
    }
  }

  ValueObjectSP MultiplyExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = PrefixExpr(std::move(rl));
    while (true) {
      if (!lhs || lhs->GetError().Fail())
        return lhs;
      switch (Token op = m_token; op.kind) {
      case TK::PipePipe:
      case TK::Asterisk:
      case TK::Slash:
      case TK::Percent:
      case TK::AsteriskAsterisk:
      case TK::AsteriskPercent:
      case TK::AsteriskPipe: {
        if (rl.IsUsed())
          return nullptr;
        NextToken();
        ValueObjectSP rhs = PrefixExpr();
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        lhs = BinaryOp(lhs, op, rhs);
        break;
      }
      default:
        return lhs;
      }
    }
  }

  ValueObjectSP PrefixExpr(ResultLoc &&rl = ResultLoc()) {
    switch (Token op = m_token; op.kind) {
    case TK::Exclamation: {
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));
      NextToken();
      ValueObjectSP rhs = PrefixExpr(
          ResultLocType(type_system->GetBasicTypeFromAST(eBasicTypeBool)));
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType rhs_type = rhs->GetCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!rhs_type_system ||
          !llvm::isa<ZigBoolType>(rhs_type_system->UnwrapType(rhs_type)))
        return nullptr;
      auto rhs_val = rhs->GetValueAsBool();
      if (!rhs_val)
        return CreateValueObject(rhs_val.takeError());
      return CreateValueObject(rhs_type_system->GetBool(!*rhs_val));
    }
    case TK::Minus:
    case TK::MinusPercent:
    case TK::Tilde: {
      NextToken();
      ValueObjectSP rhs = PrefixExpr();
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType rhs_type = rhs->GetCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!rhs_type_system)
        return nullptr;
      ZigType *rhs_zig_type = rhs_type_system->UnwrapType(rhs_type);
      switch (rhs_zig_type->GetKind()) {
      default:
        break;
      case ZigValue::Kind::ComptimeIntType: {
        auto rhs_val = rhs->GetValueAsAPSInt();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        switch (op.kind) {
        default:
          llvm_unreachable("already checked");
        case TK::Minus:
        case TK::MinusPercent:
          return CreateValueObject(rhs_type_system->GetComptimeInt(-*rhs_val));
        case TK::Tilde:
          return CreateValueObject(Status::FromErrorString(
              "unable to perform binary not operation on type 'comptime_int'"));
        }
        break;
      }
      case ZigValue::Kind::ComptimeFloatType: {
        auto rhs_val = rhs->GetValueAsAPFloat();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        switch (op.kind) {
        default:
          llvm_unreachable("already checked");
        case TK::Minus:
          return CreateValueObject(
              rhs_type_system->GetComptimeFloat(-*rhs_val));
        case TK::MinusPercent:
          return CreateValueObject(
              Status::FromErrorString("unable to perform negate wrap operation "
                                      "on type 'comptime_float'"));
        case TK::Tilde:
          return CreateValueObject(
              Status::FromErrorString("unable to perform binary not operation "
                                      "on type 'comptime_float'"));
        }
        break;
      }
      }
      return CreateValueObject(Status::FromErrorStringWithFormatv(
          "unimplemented {0}{1}", op.source, rhs_type.GetDisplayTypeName()));
    }
    case TK::Ampersand: {
      NextToken();
      ValueObjectSP rhs = PrefixExpr();
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      Status status;
      rhs = rhs->AddressOf(status);
      if (status.Fail())
        return CreateValueObject(std::move(status));
      return rhs;
    }
    case TK::Keyword:
      if (m_token.source == "try") {
        NextToken();
        ValueObjectSP rhs = PrefixExpr(std::move(rl));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        llvm_unreachable("unimplemented");
      }
      if (m_token.source == "await") {
        NextToken();
        ValueObjectSP rhs = PrefixExpr(std::move(rl));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        llvm_unreachable("unimplemented");
      }
      [[fallthrough]];
    default:
      return PrimaryExpr(std::move(rl));
    }
  }

  ValueObjectSP PrimaryExpr(ResultLoc &&rl = ResultLoc()) {
    return CurlySuffixExpr(std::move(rl));
  }

  ValueObjectSP CurlySuffixExpr(ResultLoc &&rl = ResultLoc()) {
    return TypeExpr(std::move(rl));
  }

  ValueObjectSP TypeExpr(ResultLoc &&rl = ResultLoc()) {
    switch (m_token.kind) {
    case TK::Question: {
      NextToken();
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));
      ValueObjectSP rhs = TypeExpr(ResultLocType(type_system->GetTypeType()));
      if (!rhs || rhs->GetError().Fail())
        return rhs;
      CompilerType rhs_type = rhs->GetValueAsCompilerType();
      auto rhs_type_system =
          rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
      if (!rhs_type_system)
        return nullptr;
      return CreateValueObject(
          rhs_type_system->GetOptionalType(rhs_type.GetOpaqueQualType()));
    }
    case TK::Asterisk:
    case TK::AsteriskAsterisk:
    case TK::LeftBracket: {
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));

      ZigPointerType::Size size;
      ValueObjectSP sentinel;
      bool is_allowzero = false;
      llvm::MaybeAlign pointer_align;
      ZigPointerType::AddressSpace addrspace =
          ZigPointerType::AddressSpace::Generic;
      bool is_const = false;
      bool is_volatile = false;
      bool extra_single_pointer = false;
      switch (m_token.kind) {
      default:
        llvm_unreachable("already checked");
      case TK::AsteriskAsterisk:
        extra_single_pointer = true;
        [[fallthrough]];
      case TK::Asterisk:
        NextToken();
        size = ZigPointerType::Size::One;
        break;
      case TK::LeftBracket: {
        if (ConsumeNextToken(TK::Asterisk)) {
          if (m_token.kind == TK::Identifier && m_token.source == "c") {
            NextToken();
            size = ZigPointerType::Size::C;
          } else
            size = ZigPointerType::Size::Many;
          break;
        } else if (m_token.kind == TK::Colon ||
                   m_token.kind == TK::RightBracket) {
          size = ZigPointerType::Size::Slice;
          break;
        }
        ValueObjectSP len =
            Expr(ResultLocType(type_system->GetSizeType(false)));
        if (!len || len->GetError().Fail())
          return len;
        bool success;
        uint64_t len_val = len->GetValueAsUnsigned(0, &success);
        if (!success)
          return nullptr;
        if (ConsumeToken(TK::Colon)) {
          sentinel = Expr();
          if (!sentinel || sentinel->GetError().Fail())
            return sentinel;
        }
        if (!ConsumeToken(TK::RightBracket))
          return nullptr;
        ValueObjectSP rhs = TypeExpr(ResultLocType(type_system->GetTypeType()));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        CompilerType rhs_type = rhs->GetValueAsCompilerType();
        auto rhs_type_system =
            rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!rhs_type_system)
          return nullptr;
        ZigValue *zig_sentinel = nullptr;
        if (sentinel) {
          sentinel = sentinel->Cast(rhs_type);
          if (!sentinel || sentinel->GetError().Fail())
            return sentinel;
          ValueObjectZig *zig_sentinel_valobj =
              ValueObjectZig::dyn_cast(sentinel.get());
          if (!zig_sentinel_valobj)
            return nullptr;
          zig_sentinel = zig_sentinel_valobj->UnwrapZigValue().second;
          if (!zig_sentinel)
            return nullptr;
        }
        return CreateValueObject(rhs_type_system->GetArrayType(
            len_val, zig_sentinel, rhs_type.GetOpaqueQualType()));
      }
      }
      switch (size) {
      case ZigPointerType::Size::One:
        break;
      case ZigPointerType::Size::Many:
      case ZigPointerType::Size::Slice:
        if (ConsumeToken(TK::Colon)) {
          sentinel = Expr();
          if (!sentinel || sentinel->GetError().Fail())
            return sentinel;
        }
        [[fallthrough]];
      case ZigPointerType::Size::C:
        if (!ConsumeToken(TK::RightBracket))
          return nullptr;
        break;
      }
      while (true) {
        switch (m_token.kind) {
        case TK::Keyword:
          if (m_token.source == "allowzero") {
            if (is_allowzero || size == ZigPointerType::Size::C)
              return nullptr;
            is_allowzero = true;
            NextToken();
            continue;
          }
          if (m_token.source == "align") {
            if (pointer_align || !ConsumeNextToken(TK::LeftParen))
              return nullptr;
            ValueObjectSP align_valobj = Expr(
                ResultLocType(type_system->GetBuiltinTypeForEncodingAndBitSize(
                    eEncodingUint, 29)));
            if (!align_valobj || align_valobj->GetError().Fail())
              return align_valobj;
            uint64_t align_val = align_valobj->GetValueAsUnsigned(0);
            if (align_val == 0 || !llvm::isPowerOf2_64(align_val) ||
                !ConsumeToken(TK::RightParen))
              return nullptr;
            pointer_align = llvm::Align(align_val);
            continue;
          }
          if (m_token.source == "const") {
            if (is_const)
              return nullptr;
            is_const = true;
            NextToken();
            continue;
          }
          if (m_token.source == "volatile") {
            if (is_volatile)
              return nullptr;
            is_volatile = true;
            NextToken();
            continue;
          }
          break;
        default:
          break;
        }
        ValueObjectSP rhs = TypeExpr(ResultLocType(type_system->GetTypeType()));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        CompilerType rhs_type = rhs->GetValueAsCompilerType();
        auto rhs_type_system =
            rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!rhs_type_system)
          return nullptr;
        if (sentinel) {
          sentinel = sentinel->Cast(rhs_type);
          if (!sentinel || sentinel->GetError().Fail())
            return sentinel;
        }
        ZigValue *zig_sentinel = nullptr;
        if (sentinel) {
          sentinel = sentinel->Cast(rhs_type);
          if (!sentinel || sentinel->GetError().Fail())
            return sentinel;
          ValueObjectZig *zig_sentinel_valobj =
              ValueObjectZig::dyn_cast(sentinel.get());
          if (!zig_sentinel_valobj)
            return nullptr;
          zig_sentinel = zig_sentinel_valobj->UnwrapZigValue().second;
          if (!zig_sentinel)
            return nullptr;
        }
        CompilerType result_type = rhs_type_system->GetPointerType(
            size, zig_sentinel, is_allowzero, pointer_align, addrspace,
            is_const, is_volatile, rhs_type.GetOpaqueQualType());
        if (extra_single_pointer)
          result_type = rhs_type_system->GetPointerType(
              ZigPointerType::Size::One, nullptr, false, std::nullopt,
              ZigPointerType::AddressSpace::Generic, false, false,
              result_type.GetOpaqueQualType());
        return CreateValueObject(result_type);
      }
    }
    default:
      return ErrorUnionExpr(std::move(rl));
    }
  }

  ValueObjectSP ErrorUnionExpr(ResultLoc &&rl = ResultLoc()) {
    return SuffixExpr(std::move(rl));
  }

  ValueObjectSP SuffixExpr(ResultLoc &&rl = ResultLoc()) {
    ValueObjectSP lhs = PrimaryTypeExpr(std::move(rl));
    if (!lhs || lhs->GetError().Fail())
      return lhs;
    while (true)
      switch (m_token.kind) {
      default:
        return lhs;
      case TK::LeftBracket: {
        bool success;
        Status error;
        if (rl.IsUsed())
          return nullptr;

        CompilerType lhs_type = lhs->GetCompilerType();
        auto type_system =
            lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigType *lhs_zig_type = type_system->UnwrapType(lhs_type);
        ZigValue *lhs_sentinel = nullptr;
        bool lhs_is_allowzero = false;
        llvm::Align lhs_pointer_align;
        ZigPointerType::AddressSpace lhs_addrspace =
            ZigPointerType::AddressSpace::Generic;
        bool lhs_is_const = false;
        bool lhs_is_volatile = false;
        ZigType *zig_elem_type;
        std::optional<uint64_t> lhs_len;
        CompilerType index_type;
        if (ZigPointerType *lhs_pointer_type =
                llvm::dyn_cast<ZigPointerType>(lhs_zig_type)) {
          lhs_is_allowzero = lhs_pointer_type->IsAllowZero();
          lhs_pointer_align = lhs_pointer_type->GetPointerAlign();
          lhs_is_const = lhs_pointer_type->IsConst();
          lhs_is_volatile = lhs_pointer_type->IsVolatile();
          zig_elem_type = lhs_pointer_type->GetChildType();
          switch (lhs_pointer_type->GetSize()) {
          case ZigPointerType::Size::One:
            if (ZigSequenceType *lhs_sequence_type =
                    llvm::dyn_cast<ZigSequenceType>(zig_elem_type)) {
              zig_elem_type = lhs_sequence_type->GetChildType();
              lhs_len = lhs_sequence_type->GetLength();
              lhs_sentinel = lhs_sequence_type->GetSentinel();
              break;
            }
            return nullptr;
          case ZigPointerType::Size::Many:
            lhs_sentinel = lhs_pointer_type->GetSentinel();
            break;
          case ZigPointerType::Size::C:
            break;
          case ZigPointerType::Size::Slice: {
            ValueObjectSP len = lhs->GetChildMemberWithName("len");
            if (!len || len->GetError().Fail())
              return len;
            lhs_len = len->GetValueAsUnsigned(0, &success);
            if (!success)
              return nullptr;
            lhs_sentinel = lhs_pointer_type->GetSentinel();
            lhs = lhs->GetChildMemberWithName("ptr");
            if (!lhs || lhs->GetError().Fail())
              return lhs;
            break;
          }
          }
        } else if (ZigSequenceType *lhs_sequence_type =
                       llvm::dyn_cast<ZigSequenceType>(lhs_zig_type)) {
          zig_elem_type = lhs_sequence_type->GetChildType();
          lhs_len = lhs_sequence_type->GetLength();
          lhs_sentinel = lhs_sequence_type->GetSentinel();
        } else if (ZigTupleType *lhs_tuple_type =
                       llvm::dyn_cast<ZigTupleType>(lhs_zig_type)) {
          zig_elem_type = nullptr;
          lhs_len = lhs_tuple_type->GetFields().size();
          index_type = type_system->GetComptimeIntType();
        } else
          return nullptr;
        if (!index_type)
          index_type = type_system->GetSizeType(false);

        NextToken();
        ValueObjectSP start = Expr(ResultLocType(index_type));
        if (!start || start->GetError().Fail())
          return start;
        uint64_t start_val = start->GetValueAsUnsigned(0, &success);
        if (!success)
          return nullptr;
        if (ConsumeToken(TK::DotDot)) {
          if (!zig_elem_type)
            return nullptr;

          std::optional<uint64_t> end_val;
          switch (m_token.kind) {
          case TK::RightBracket:
          case TK::Colon:
            end_val = lhs_len;
            if (end_val && start_val > *end_val)
              return nullptr;
            break;
          default: {
            ValueObjectSP end = Expr(ResultLocType(index_type));
            if (!end || end->GetError().Fail())
              return end;
            end_val = end->GetValueAsUnsigned(0, &success);
            if (!success || start_val > *end_val)
              return nullptr;
            if (lhs_len) {
              uint64_t lhs_len_including_sentinel =
                  *lhs_len + (lhs_sentinel != nullptr);
              if (start_val > lhs_len_including_sentinel ||
                  *end_val > lhs_len_including_sentinel)
                return nullptr;
            }
            break;
          }
          }
          ZigValue *sentinel;
          if (ConsumeToken(TK::Colon)) {
            CompilerType elem_type = type_system->WrapType(zig_elem_type);
            ValueObjectSP sentinel_valobj =
                Expr(ResultLocType(elem_type))->Cast(elem_type);
            if (!sentinel_valobj || sentinel_valobj->GetError().Fail())
              return sentinel_valobj;
            sentinel = ValueObjectZig::dyn_cast(sentinel_valobj.get())
                           ->UnwrapZigValue()
                           .second;
            if (!sentinel)
              return nullptr;
          } else if (end_val == lhs_len)
            sentinel = lhs_sentinel;
          else
            sentinel = nullptr;

          uint64_t offset = zig_elem_type->GetByteSize() * start_val;
          if (end_val) {
            CompilerType array_type = type_system->GetArrayType(
                *end_val - start_val, sentinel, zig_elem_type->AsOpaqueType());
            CompilerType pointer_type = type_system->GetPointerType(
                ZigPointerType::Size::One, nullptr, lhs_is_allowzero,
                commonAlignment(lhs_pointer_align, offset), lhs_addrspace,
                lhs_is_const, lhs_is_volatile, array_type.GetOpaqueQualType());
            if (ValueObjectZig *lhs_zig = ValueObjectZig::dyn_cast(lhs.get())) {
              auto lhs_zig_value = lhs_zig->UnwrapZigValue().second;
              if (!lhs_zig_value)
                return nullptr;
              if (ZigPointer *zig_pointer =
                      llvm::dyn_cast<ZigPointer>(lhs_zig_value)) {
                offset += zig_pointer->GetOffset();
                lhs_zig_value = zig_pointer->GetPointee();
              }
              lhs = CreateValueObject(type_system->GetPointer(
                  llvm::cast<ZigPointerType>(
                      type_system->UnwrapType(pointer_type)),
                  lhs_zig_value, offset));
            } else {
              addr_t ptr_addr = lhs->GetPointerValue();
              if ((!lhs_is_allowzero && ptr_addr == 0) ||
                  ptr_addr == LLDB_INVALID_ADDRESS)
                return nullptr;
              lhs = ValueObject::CreateValueObjectFromAddress(
                  llvm::formatv("[{0:d}..{1:d}]", start_val, *end_val).str(),
                  ptr_addr + offset, m_exe_ctx, pointer_type, false);
            }
          } else {
            CompilerType pointer_type = type_system->GetPointerType(
                ZigPointerType::Size::Many, sentinel, lhs_is_allowzero,
                commonAlignment(lhs_pointer_align, offset), lhs_addrspace,
                lhs_is_const, lhs_is_volatile, zig_elem_type->AsOpaqueType());
            addr_t ptr_addr;
            if (llvm::isa<ZigPointerType>(lhs_zig_type))
              ptr_addr = lhs->GetPointerValue();
            else
              ptr_addr = lhs->GetAddressOf();
            if ((!lhs_is_allowzero && ptr_addr == 0) ||
                ptr_addr == LLDB_INVALID_ADDRESS)
              return nullptr;
            lhs = ValueObject::CreateValueObjectFromAddress(
                llvm::formatv("[{0:d}..]", start_val).str(), ptr_addr + offset,
                m_exe_ctx, pointer_type, false);
          }
        } else {
          if (lhs_len && start_val >= *lhs_len + (lhs_sentinel != nullptr))
            return nullptr;
          if (ZigPointerType *lhs_pointer_type =
                  llvm::dyn_cast<ZigPointerType>(lhs_zig_type))
            switch (lhs_pointer_type->GetSize()) {
            case ZigPointerType::Size::One:
              lhs = lhs->GetChildAtIndex(0);
              if (!lhs || lhs->GetError().Fail())
                return lhs;
              lhs = lhs->GetChildAtIndex(start_val);
              break;
            case ZigPointerType::Size::Many:
            case ZigPointerType::Size::C:
            case ZigPointerType::Size::Slice:
              lhs = lhs->GetSyntheticArrayMember(start_val, true);
              break;
            }
          else
            lhs = lhs->GetChildAtIndex(start_val);
        }
        if (!lhs || lhs->GetError().Fail())
          return lhs;
        if (!ConsumeToken(TK::RightBracket))
          return nullptr;
        break;
      }
      case TK::Dot:
        if (rl.IsUsed())
          return nullptr;
        switch (NextToken().kind) {
        default:
          return nullptr;
        case TK::Identifier:
        case TK::QuotedIdentifier: {
          CompilerType lhs_type = lhs->GetCompilerType();
          auto type_system =
              lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
          if (!type_system)
            return nullptr;
          ZigType *lhs_zig_type = type_system->UnwrapType(lhs_type);
          if (llvm::isa<ZigTypeType>(lhs_zig_type) ||
              llvm::isa<ZigTupleType>(lhs_zig_type) ||
              llvm::isa<ZigRecordType>(lhs_zig_type))
            lhs = lhs->GetNonSyntheticValue()->GetChildMemberWithName(
                m_token.parsed);
          else if (ZigPointerType *lhs_pointer_type =
                       llvm::dyn_cast<ZigPointerType>(lhs_zig_type))
            switch (lhs_pointer_type->GetSize()) {
            case ZigPointerType::Size::One: {
              ZigType *child_type = lhs_pointer_type->GetChildType();
              if (llvm::isa<ZigArrayType>(child_type))
                lhs = lhs->GetSyntheticValue()->GetChildMemberWithName(
                    m_token.parsed);
              else if (llvm::isa<ZigTupleType>(child_type) ||
                       llvm::isa<ZigRecordType>(child_type))
                lhs = lhs->GetNonSyntheticValue()->GetChildMemberWithName(
                    m_token.parsed);
              break;
            }
            case ZigPointerType::Size::Many:
            case ZigPointerType::Size::C:
              return nullptr;
            case ZigPointerType::Size::Slice:
              lhs = lhs->GetNonSyntheticValue()->GetChildMemberWithName(
                  m_token.parsed);
              break;
            }
          else
            return nullptr;
          if (!lhs || lhs->GetError().Fail())
            return lhs;
          NextToken();
          break;
        }
        }
        break;
      case TK::DotAsterisk: {
        if (rl.IsUsed())
          return nullptr;
        Status error;
        lhs = lhs->Dereference(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        if (!lhs || lhs->GetError().Fail())
          return lhs;
        NextToken();
        break;
      }
      case TK::DotQuestion:
        if (rl.IsUsed())
          return nullptr;
        lhs = lhs->GetChildMemberWithName("?");
        if (!lhs || lhs->GetError().Fail())
          return lhs;
        NextToken();
        break;
      }
  }

  ValueObjectSP PrimaryTypeExpr(ResultLoc &&rl = ResultLoc()) {
    switch (m_token.kind) {
    case TK::Builtin:
      if (m_token.source == "@alignCast" || m_token.source == "@ptrCast") {
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ResultLocType *rlt = llvm::dyn_cast<ResultLocType>(&rl);
        if (!rlt)
          return CreateValueObject(Status::FromErrorString(
              "@ptrCast must have a known result type"));
        CompilerType type = rlt->GetType();
        auto type_system =
            type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigPointerType *ptr_type = llvm::dyn_cast_if_present<ZigPointerType>(
            type_system->UnwrapType(type));
        if (!ptr_type || ptr_type->GetSize() == ZigPointerType::Size::Slice)
          return nullptr;
        ValueObjectSP ptr_valobj = Expr(ResultLocType(type));
        if (!ptr_valobj || ptr_valobj->GetError().Fail())
          return ptr_valobj;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        if (ValueObjectZig *zig_ptr_valobj =
                ValueObjectZig::dyn_cast(ptr_valobj.get()))
          if (ZigPointer *zig_ptr_value = llvm::dyn_cast_if_present<ZigPointer>(
                  zig_ptr_valobj->UnwrapZigValue().second))
            return CreateValueObject(
                type_system->GetPointer(ptr_type, zig_ptr_value->GetPointee(),
                                        zig_ptr_value->GetOffset()));
        addr_t ptr_addr = ptr_valobj->GetPointerValue();
        if (ptr_addr == LLDB_INVALID_ADDRESS)
          return nullptr;
        return ValueObject::CreateValueObjectFromAddress(
            ptr_valobj->GetName(), ptr_addr, m_exe_ctx, type, false);
      }
      if (m_token.source == "@as") {
        Status error;
        auto type_system = GetScratchTypeSystem(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP type_valobj =
            Expr(ResultLocType(type_system->GetTypeType()));
        if (!type_valobj || type_valobj->GetError().Fail())
          return type_valobj;
        CompilerType type = type_valobj->GetValueAsCompilerType();
        if (!type || !ConsumeToken(TK::Comma))
          return nullptr;
        ValueObjectSP result = Expr(ResultLocType(type));
        if (!result || result->GetError().Fail())
          return result;
        result = result->Cast(type);
        if (!result || result->GetError().Fail())
          return result;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        return result;
      }
      if (m_token.source == "@bitSizeOf") {
        Status error;
        auto type_system = GetScratchTypeSystem(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP type_valobj = Expr();
        if (!type_valobj || type_valobj->GetError().Fail())
          return type_valobj;
        CompilerType type = type_valobj->GetValueAsCompilerType();
        auto type_type_system =
            type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        auto bit_size =
            type.GetBitSize(m_exe_ctx.GetBestExecutionContextScope());
        if (!type_type_system || !bit_size || !ConsumeToken(TK::RightParen))
          return nullptr;
        return CreateValueObject(type_type_system->GetComptimeInt(
            llvm::APSInt(llvm::APInt(64, *bit_size))));
      }
      if (m_token.source == "@enumFromInt") {
        ResultLocType *rlt = llvm::dyn_cast<ResultLocType>(&rl);
        if (!rlt)
          return CreateValueObject(Status::FromErrorString(
              "@enumFromInt must have a known result type"));
        auto type_system =
            rlt->GetType().GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigTagType *enum_type =
            llvm::dyn_cast<ZigTagType>(type_system->UnwrapType(rlt->GetType()));
        if (!enum_type || llvm::isa<ZigErrorSetType>(enum_type))
          return CreateValueObject(Status::FromErrorStringWithFormatv(
              "expected enum, found '{}'",
              rlt->GetType().GetDisplayTypeName()));
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP int_valobj = Expr();
        if (!int_valobj || int_valobj->GetError().Fail())
          return int_valobj;
        auto int_val = int_valobj->GetValueAsAPSInt();
        if (!int_val)
          return CreateValueObject(int_val.takeError());
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        return CreateValueObject(type_system->GetTag(
            enum_type, int_val->extOrTrunc(enum_type->GetBitSize())));
      }
      if (m_token.source == "@intFromEnum") {
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP enum_valobj = Expr();
        if (!enum_valobj || enum_valobj->GetError().Fail())
          return enum_valobj;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        CompilerType enum_type = enum_valobj->GetCompilerType();
        auto type_system =
            enum_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigType *zig_enum_type = type_system->UnwrapType(enum_type);
        if (!llvm::isa_and_present<ZigGeneratedTagType>(zig_enum_type) &&
            !llvm::isa_and_present<ZigEnumType>(zig_enum_type))
          return nullptr;
        auto enum_val = enum_valobj->GetValueAsAPSInt();
        if (!enum_val)
          return CreateValueObject(enum_val.takeError());
        return CreateValueObject(type_system->GetInt(
            llvm::cast<ZigTagType>(zig_enum_type)->GetBackingType(),
            *enum_val));
      }
      if (m_token.source == "@intFromError") {
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP error_valobj = Expr();
        if (!error_valobj || error_valobj->GetError().Fail())
          return error_valobj;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        CompilerType error_type = error_valobj->GetCompilerType();
        auto type_system =
            error_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigType *zig_error_type = type_system->UnwrapType(error_type);
        if (ZigErrorUnionType *zig_error_union_type =
                llvm::dyn_cast_if_present<ZigErrorUnionType>(zig_error_type)) {
          error_valobj = error_valobj->GetChildMemberWithName("error");
          if (!error_valobj || error_valobj->GetError().Fail())
            return error_valobj;
          zig_error_type = zig_error_union_type->GetErrorSet();
        } else if (!llvm::isa_and_present<ZigErrorSetType>(zig_error_type))
          return nullptr;
        auto error_val = error_valobj->GetValueAsAPSInt();
        if (!error_val)
          return CreateValueObject(error_val.takeError());
        if (error_val->isZero())
          return CreateValueObject(
              Status::FromErrorString("error union does not contain an error"));
        return CreateValueObject(type_system->GetInt(
            llvm::cast<ZigErrorSetType>(zig_error_type)->GetBackingType(),
            *error_val));
      }
      if (m_token.source == "@intFromPtr") {
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP ptr_valobj = Expr();
        if (!ptr_valobj || ptr_valobj->GetError().Fail())
          return ptr_valobj;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        CompilerType ptr_type = ptr_valobj->GetCompilerType();
        auto type_system =
            ptr_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!type_system)
          return nullptr;
        ZigType *zig_type = type_system->UnwrapType(ptr_type);
        if (ZigOptionalType *zig_opt_type =
                llvm::dyn_cast_if_present<ZigOptionalType>(zig_type)) {
          ptr_valobj = ptr_valobj->GetChildMemberWithName("?");
          if (!ptr_valobj || ptr_valobj->GetError().Fail())
            return ptr_valobj;
          zig_type = zig_opt_type->GetChildType();
        }
        ZigPointerType *zig_ptr_type =
            llvm::dyn_cast_if_present<ZigPointerType>(zig_type);
        if (!zig_ptr_type ||
            zig_ptr_type->GetSize() == ZigPointerType::Size::Slice)
          return nullptr;
        auto ptr_val = ptr_valobj->GetValueAsAPSInt();
        if (!ptr_val)
          return CreateValueObject(ptr_val.takeError());
        return CreateValueObject(
            type_system->GetInt(llvm::cast<ZigIntType>(type_system->UnwrapType(
                                    type_system->GetSizeType(false))),
                                *ptr_val));
      }
      if (m_token.source == "@sizeOf") {
        Status error;
        auto type_system = GetScratchTypeSystem(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP type_valobj = Expr();
        if (!type_valobj || type_valobj->GetError().Fail())
          return type_valobj;
        CompilerType type = type_valobj->GetValueAsCompilerType();
        auto type_type_system =
            type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        auto byte_size =
            type.GetByteSize(m_exe_ctx.GetBestExecutionContextScope());
        if (!type_type_system || !byte_size || !ConsumeToken(TK::RightParen))
          return nullptr;
        return CreateValueObject(type_type_system->GetComptimeInt(
            llvm::APSInt(llvm::APInt(64, *byte_size))));
      }
      if (m_token.source == "@This") {
        if (!m_ctx_obj || !ConsumeNextToken(TK::LeftParen) ||
            !ConsumeToken(TK::RightParen))
          return nullptr;
        CompilerType ctx_type = m_ctx_obj->GetCompilerType();
        auto ctx_type_system =
            ctx_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!ctx_type_system)
          return nullptr;
        return CreateValueObject(ctx_type_system->UnwrapType(ctx_type));
      }
      if (m_token.source == "@this") {
        if (!m_ctx_obj || !ConsumeNextToken(TK::LeftParen) ||
            !ConsumeToken(TK::RightParen))
          return nullptr;
        return m_ctx_obj->GetSP();
      }
      if (m_token.source == "@TypeOf") {
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP expr = Expr();
        if (!expr || expr->GetError().Fail())
          return expr;
        if (!ConsumeToken(TK::RightParen))
          return nullptr;
        return CreateValueObject(expr->GetCompilerType());
      }
      if (m_token.source == "@Vector") {
        Status error;
        auto type_system = GetScratchTypeSystem(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        if (!ConsumeNextToken(TK::LeftParen))
          return nullptr;
        ValueObjectSP len =
            Expr(ResultLocType(type_system->GetBuiltinTypeForEncodingAndBitSize(
                eEncodingUint, 32)));
        if (!len || len->GetError().Fail())
          return len;
        bool success;
        uint32_t len_val = len->GetValueAsUnsigned(0, &success);
        if (!success || !ConsumeToken(TK::Comma))
          return nullptr;
        ValueObjectSP elem = Expr(ResultLocType(type_system->GetTypeType()));
        if (!elem || elem->GetError().Fail())
          return elem;
        CompilerType elem_type = elem->GetValueAsCompilerType();
        auto elem_type_system =
            elem_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
        if (!elem_type_system || !ConsumeToken(TK::RightParen))
          return nullptr;
        return CreateValueObject(elem_type_system->GetVectorType(
            len_val, elem_type.GetOpaqueQualType()));
      }
      return nullptr;
    case TK::NumberLiteral: {
      llvm::StringRef str = m_token.parsed;
      unsigned radix = 10;
      if (str.consume_front("0b"))
        radix = 2;
      else if (str.consume_front("0o"))
        radix = 8;
      else if (str.consume_front("0x"))
        radix = 16;
      llvm::APInt int_val;
      if (str.getAsInteger(radix, int_val)) {
        if (str.starts_with("0X"))
          return nullptr;
        llvm::APFloat float_val(llvm::APFloat::IEEEquad());
        if (auto convert_result = float_val.convertFromString(
                m_token.parsed, llvm::RoundingMode::NearestTiesToEven);
            !convert_result)
          return CreateValueObject(convert_result.takeError());
        Status error;
        auto type_system = GetScratchTypeSystem(error);
        if (error.Fail())
          return CreateValueObject(std::move(error));
        NextToken();
        return CreateValueObject(type_system->GetComptimeFloat(float_val));
      }
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));
      NextToken();
      return CreateValueObject(
          type_system->GetComptimeInt(llvm::APSInt(int_val)));
    }
    case TK::LeftParen: {
      NextToken();
      ValueObjectSP result = Expr(std::move(rl));
      if (!result || result->GetError().Fail())
        return result;
      if (!ConsumeToken(TK::RightParen))
        return nullptr;
      return result;
    }
    case TK::QuotedIdentifier:
    case TK::Identifier:
      if (StackFrame *frame = m_exe_ctx.GetFramePtr()) {
        SymbolContext sym_ctx = frame->GetSymbolContext(eSymbolContextFunction |
                                                        eSymbolContextBlock);
        if (ValueObjectSP valobj_sp =
                LookupLocalName(ConstString(m_token.parsed), frame, sym_ctx)) {
          NextToken();
          return valobj_sp;
        }
      }
      return nullptr;
    case TK::Primitive: {
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));
      llvm::StringRef primitive = m_token.source;
      NextToken();
      if (primitive == "anyopaque")
        return CreateValueObject(type_system->GetAnyOpaqueType());
      if (primitive == "bool")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeBool));
      if (primitive == "c_int")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeInt));
      if (primitive == "c_long")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeLong));
      if (primitive == "c_longdouble")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeLongDouble));
      if (primitive == "c_longlong")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeLongLong));
      if (primitive == "c_char")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeChar));
      if (primitive == "c_short")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeShort));
      if (primitive == "c_uint")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeUnsignedInt));
      if (primitive == "c_ulong")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeUnsignedLong));
      if (primitive == "c_ulonglong")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeUnsignedLongLong));
      if (primitive == "c_ushort")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeUnsignedShort));
      if (primitive == "comptime_float")
        return CreateValueObject(type_system->GetComptimeFloatType());
      if (primitive == "comptime_int")
        return CreateValueObject(type_system->GetComptimeIntType());
      if (primitive == "false")
        return CreateValueObject(type_system->GetBool(false));
      if (primitive == "isize")
        return CreateValueObject(type_system->GetSizeType(true));
      if (primitive == "noreturn")
        return CreateValueObject(type_system->GetNoReturnType());
      if (primitive == "null")
        return CreateValueObject(type_system->GetNull());
      if (primitive == "true")
        return CreateValueObject(type_system->GetBool(true));
      if (primitive == "type")
        return CreateValueObject(type_system->GetTypeType());
      if (primitive == "undefined")
        return CreateValueObject(type_system->GetUndefined());
      if (primitive == "usize")
        return CreateValueObject(type_system->GetSizeType(false));
      if (primitive == "void")
        return CreateValueObject(
            type_system->GetBasicTypeFromAST(eBasicTypeVoid));
      Encoding encoding;
      switch (primitive[0]) {
      default:
        return nullptr;
      case 'f':
        encoding = eEncodingIEEE754;
        break;
      case 'i':
        encoding = eEncodingSint;
        break;
      case 'u':
        encoding = eEncodingUint;
        break;
      }
      uint16_t bit_size;
      if (primitive.substr(1).getAsInteger(10, bit_size))
        return nullptr;
      return CreateValueObject(
          type_system->GetBuiltinTypeForEncodingAndBitSize(encoding, bit_size));
    }
    case TK::StringLiteral: {
      Status error;
      auto type_system = GetScratchTypeSystem(error);
      if (error.Fail())
        return CreateValueObject(std::move(error));
      ZigIntType *u8_type =
          llvm::cast_if_present<ZigIntType>(type_system->UnwrapType(
              type_system->GetBuiltinTypeForEncodingAndBitSize(eEncodingUint,
                                                               8)));
      if (!u8_type)
        return nullptr;
      ZigArrayType *array_type = llvm::cast_if_present<ZigArrayType>(
          type_system->UnwrapType(type_system->GetArrayType(
              m_token.parsed.size() - 1,
              type_system->GetInt(u8_type, llvm::APInt(8, 0)),
              u8_type->AsOpaqueType())));
      if (!array_type)
        return nullptr;
      ZigValue *array_value = type_system->GetData(
          array_type, llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(
                                                  m_token.parsed.data()),
                                              m_token.parsed.size()));
      NextToken();
      ZigPointerType *pointer_type = llvm::cast_if_present<ZigPointerType>(
          type_system->UnwrapType(type_system->GetPointerType(
              ZigPointerType::Size::One, nullptr, false, std::nullopt,
              ZigPointerType::AddressSpace::Generic, true, false, array_type)));
      return CreateValueObject(
          type_system->GetPointer(pointer_type, array_value, 0));
    }
    default:
      return nullptr;
    }
  }

private:
  bool ConsumeNextToken(TK kind) {
    NextToken();
    return ConsumeToken(kind);
  }
  bool ConsumeToken(TK kind) {
    if (m_token.kind != kind)
      return false;
    NextToken();
    return true;
  }
  bool ConsumeKeyword(llvm::StringRef keyword) {
    if (m_token.kind != TK::Keyword || m_token.source != keyword)
      return false;
    NextToken();
    return true;
  }
  Token NextToken() {
    do
      m_token = m_lexer.NextToken();
    while (m_token.kind == TK::Comment);
    return m_token;
  }

  ValueObjectSP CreateValueObject(llvm::Error &&error) {
    return CreateValueObject(Status::FromError(std::move(error)));
  }

  ValueObjectSP CreateValueObject(Status &&error) {
    if (error.Success())
      return nullptr;
    return ValueObjectConstResult::Create(
        m_exe_ctx.GetBestExecutionContextScope(), std::move(error));
  }

  ValueObjectSP CreateValueObject(ZigValue *value) {
    return ValueObjectZig::Create(m_exe_ctx.GetBestExecutionContextScope(),
                                  value);
  }

  ValueObjectSP CreateValueObject(CompilerType type) {
    return ValueObjectZig::Create(m_exe_ctx.GetBestExecutionContextScope(),
                                  type);
  }

  std::shared_ptr<TypeSystemZig> GetScratchTypeSystem(Status &error) {
    Target *target = m_exe_ctx.GetTargetPtr();
    if (!target) {
      error = Status::FromErrorString("no target has been created");
      return nullptr;
    }
    llvm::Expected<TypeSystemSP> type_system_or_err =
        target->GetScratchTypeSystemForLanguage(eLanguageTypeZig);
    if (!type_system_or_err) {
      error = Status::FromError(type_system_or_err.takeError());
      return nullptr;
    }
    TypeSystemZig *type_system =
        llvm::dyn_cast<TypeSystemZig>(type_system_or_err->get());
    if (!type_system_or_err) {
      error = Status::FromErrorString("unexpected type system type");
      return nullptr;
    }
    error.Clear();
    return {std::move(*type_system_or_err), type_system};
  }

  static ValueObjectSP LookupLocalName(ConstString name, StackFrame *frame,
                                       SymbolContext &sym_ctx) {
    if (!sym_ctx.block)
      return nullptr;

    CompilerDeclContext decl_context = sym_ctx.block->GetDeclContext();
    if (!decl_context)
      return nullptr;

    VariableListSP vars = frame->GetInScopeVariableList(true);
    for (VariableSP var : *vars)
      var->GetDecl();

    std::vector<CompilerDecl> found_decls =
        decl_context.FindDeclByName(name, false);

    for (CompilerDecl decl : found_decls) {
      if (TypeSystemZig *type_system =
              llvm::dyn_cast<TypeSystemZig>(decl.GetTypeSystem()))
        if (ValueObjectSP valobj_sp = type_system->DeclGetConstantValueObject(
                decl.GetOpaqueDecl(), frame))
          return valobj_sp;

      for (VariableSP var : *vars)
        if (var->GetDecl() == decl)
          return ValueObjectVariable::Create(frame, var);
    }
    return nullptr;
  }

  ValueObjectSP BinaryOp(ValueObjectSP lhs, Token op, ValueObjectSP rhs) {
    CompilerType lhs_type = lhs->GetCompilerType();
    auto lhs_type_system =
        lhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
    CompilerType rhs_type = rhs->GetCompilerType();
    auto rhs_type_system =
        rhs_type.GetTypeSystem().dyn_cast_if_present<TypeSystemZig>();
    if (!lhs_type_system || !rhs_type_system)
      return nullptr;
    ZigType *lhs_zig_type = lhs_type_system->UnwrapType(lhs_type);
    ZigType *rhs_zig_type = rhs_type_system->UnwrapType(rhs_type);
    if (!lhs_zig_type || !rhs_zig_type)
      return nullptr;
    switch (lhs_zig_type->GetKind()) {
    default:
      break;
    case ZigValue::Kind::ComptimeIntType: {
      switch (rhs_zig_type->GetKind()) {
      default:
        break;
      case ZigValue::Kind::ComptimeIntType: {
        auto lhs_val = lhs->GetValueAsAPSInt();
        if (!lhs_val)
          return CreateValueObject(lhs_val.takeError());
        auto rhs_val = rhs->GetValueAsAPSInt();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        unsigned lhs_bits =
            lhs_val->isZero() ? 0 : lhs_val->getSignificantBits();
        unsigned rhs_bits =
            rhs_val->isZero() ? 0 : rhs_val->getSignificantBits();
        switch (op.kind) {
        default:
          break;
        case TK::Ampersand: {
          unsigned bits = std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) & rhs_val->extOrTrunc(bits)));
        }
        case TK::Caret: {
          unsigned bits = std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) ^ rhs_val->extOrTrunc(bits)));
        }
        case TK::Pipe: {
          unsigned bits = std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) | rhs_val->extOrTrunc(bits)));
        }
        case TK::LeftArrowLeftArrow:
        case TK::LeftArrowLeftArrowPipe: {
          if (rhs_val->isZero())
            return lhs;
          if (rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorStringWithFormatv(
                "shift by negative amount '{0}'", *rhs_val));
          if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
            return CreateValueObject(lhs_type_system->GetComptimeInt(
                lhs_val->extOrTrunc(lhs_bits + *shift_amt) << *shift_amt));
          break;
        }
        case TK::RightArrowRightArrow:
          if (rhs_val->isZero())
            return lhs;
          if (rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorStringWithFormatv(
                "shift by negative amount '{0}'", *rhs_val));
          if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
            return CreateValueObject(
                lhs_type_system->GetComptimeInt(*lhs_val >> *shift_amt));
          break;
        case TK::Plus:
        case TK::PlusPercent:
        case TK::PlusPipe: {
          unsigned bits = 1 + std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) + rhs_val->extOrTrunc(bits)));
        }
        case TK::Minus:
        case TK::MinusPercent:
        case TK::MinusPipe: {
          unsigned bits = 1 + std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) - rhs_val->extOrTrunc(bits)));
        }
        case TK::Asterisk:
        case TK::AsteriskPercent:
        case TK::AsteriskPipe: {
          unsigned bits = lhs_bits + rhs_bits;
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) * rhs_val->extOrTrunc(bits)));
        }
        case TK::Slash: {
          if (rhs_val->isZero())
            return CreateValueObject(Status::FromErrorString(
                "division by zero here causes undefined behavior"));
          unsigned bits = std::max(1 + lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) / rhs_val->extOrTrunc(bits)));
        }
        case TK::Percent: {
          if (rhs_val->isZero())
            return CreateValueObject(Status::FromErrorString(
                "division by zero here causes undefined behavior"));
          if (lhs_val->isNegative() || rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorString(
                "remainder division with 'comptime_int' and 'comptime_int': "
                "signed integers and floats must use @rem or @mod"));
          unsigned bits = std::max(lhs_bits, rhs_bits);
          return CreateValueObject(lhs_type_system->GetComptimeInt(
              lhs_val->extOrTrunc(bits) % rhs_val->extOrTrunc(bits)));
        }
        }
        break;
      }
      case ZigValue::Kind::IntType:
        switch (op.kind) {
        default: {
          lhs = lhs->Cast(rhs_type);
          if (!lhs || lhs->GetError().Fail())
            return lhs;
          auto lhs_val = lhs->GetValueAsAPSInt();
          if (!lhs_val)
            return CreateValueObject(lhs_val.takeError());
          auto rhs_val = rhs->GetValueAsAPSInt();
          if (!rhs_val)
            return CreateValueObject(rhs_val.takeError());
          ZigIntType *int_type = llvm::cast<ZigIntType>(rhs_zig_type);
          Status error;
          llvm::APSInt result =
              BinaryOpInt(int_type, *lhs_val, op, *rhs_val, error);
          if (error.Fail())
            return CreateValueObject(std::move(error));
          return CreateValueObject(rhs_type_system->GetInt(int_type, result));
          break;
        }
        case TK::LeftArrowLeftArrow:
        case TK::LeftArrowLeftArrowPipe: {
          auto lhs_val = lhs->GetValueAsAPSInt();
          if (!lhs_val)
            return CreateValueObject(lhs_val.takeError());
          auto rhs_val = rhs->GetValueAsAPSInt();
          if (!rhs_val)
            return CreateValueObject(rhs_val.takeError());
          if (rhs_val->isZero())
            return lhs;
          if (rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorStringWithFormatv(
                "shift by negative amount '{0}'", *rhs_val));
          unsigned lhs_bits =
              lhs_val->isZero() ? 0 : lhs_val->getSignificantBits();
          if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
            return CreateValueObject(lhs_type_system->GetComptimeInt(
                lhs_val->extOrTrunc(lhs_bits + *shift_amt) << *shift_amt));
          break;
        }
        case TK::RightArrowRightArrow: {
          auto lhs_val = lhs->GetValueAsAPSInt();
          if (!lhs_val)
            return CreateValueObject(lhs_val.takeError());
          auto rhs_val = rhs->GetValueAsAPSInt();
          if (!rhs_val)
            return CreateValueObject(rhs_val.takeError());
          if (rhs_val->isZero())
            return lhs;
          if (rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorStringWithFormatv(
                "shift by negative amount '{0}'", *rhs_val));
          if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
            return CreateValueObject(
                lhs_type_system->GetComptimeInt(*lhs_val >> *shift_amt));
          break;
        }
        }
        break;
      }
      break;
    }
    case ZigValue::Kind::ComptimeFloatType: {
      auto lhs_val = lhs->GetValueAsAPFloat();
      if (!lhs_val)
        return CreateValueObject(lhs_val.takeError());
      switch (rhs_zig_type->GetKind()) {
      default:
        break;
      case ZigValue::Kind::ComptimeFloatType: {
        auto rhs_val = rhs->GetValueAsAPFloat();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        switch (op.kind) {
        case TK::Plus:
          return CreateValueObject(
              lhs_type_system->GetComptimeFloat(*lhs_val + *rhs_val));
        case TK::Minus:
          return CreateValueObject(
              lhs_type_system->GetComptimeFloat(*lhs_val - *rhs_val));
        case TK::Asterisk:
          return CreateValueObject(
              lhs_type_system->GetComptimeFloat(*lhs_val * *rhs_val));
        case TK::Slash:
          return CreateValueObject(
              lhs_type_system->GetComptimeFloat(*lhs_val / *rhs_val));
        case TK::Percent: {
          if (lhs_val->isNegative() || rhs_val->isNegative())
            return CreateValueObject(Status::FromErrorString(
                "remainder division with 'comptime_int' and 'comptime_int': "));
          lhs_val->mod(*rhs_val);
          return CreateValueObject(lhs_type_system->GetComptimeFloat(*lhs_val));
        }
        default:
          break;
        }
      }
      }
      break;
    }
    case ZigValue::Kind::IntType:
      switch (op.kind) {
      default:
        switch (rhs_zig_type->GetKind()) {
        default:
          break;
        case ZigValue::Kind::ComptimeIntType: {
          auto lhs_val = lhs->GetValueAsAPSInt();
          if (!lhs_val)
            return CreateValueObject(lhs_val.takeError());
          rhs = rhs->Cast(lhs_type);
          if (!rhs || rhs->GetError().Fail())
            return rhs;
          auto rhs_val = rhs->GetValueAsAPSInt();
          if (!rhs_val)
            return CreateValueObject(rhs_val.takeError());
          ZigIntType *int_type = llvm::cast<ZigIntType>(lhs_zig_type);
          Status error;
          llvm::APSInt result =
              BinaryOpInt(int_type, *lhs_val, op, *rhs_val, error);
          if (error.Fail())
            return CreateValueObject(std::move(error));
          return CreateValueObject(lhs_type_system->GetInt(int_type, result));
        }
        case ZigValue::Kind::IntType:
          if (lhs_zig_type->GetBitSize() >= rhs_zig_type->GetBitSize()) {
            auto lhs_val = lhs->GetValueAsAPSInt();
            if (!lhs_val)
              return CreateValueObject(lhs_val.takeError());
            rhs = rhs->Cast(lhs_type);
            if (!rhs || rhs->GetError().Fail())
              return rhs;
            auto rhs_val = rhs->GetValueAsAPSInt();
            if (!rhs_val)
              return CreateValueObject(rhs_val.takeError());
            ZigIntType *int_type = llvm::cast<ZigIntType>(lhs_zig_type);
            Status error;
            llvm::APSInt result =
                BinaryOpInt(int_type, *lhs_val, op, *rhs_val, error);
            if (error.Fail())
              return CreateValueObject(std::move(error));
            return CreateValueObject(lhs_type_system->GetInt(int_type, result));
          } else {
            lhs = lhs->Cast(rhs_type);
            if (!lhs || lhs->GetError().Fail())
              return rhs;
            auto lhs_val = lhs->GetValueAsAPSInt();
            if (!lhs_val)
              return CreateValueObject(lhs_val.takeError());
            auto rhs_val = rhs->GetValueAsAPSInt();
            if (!rhs_val)
              return CreateValueObject(rhs_val.takeError());
            ZigIntType *int_type = llvm::cast<ZigIntType>(rhs_zig_type);
            Status error;
            llvm::APSInt result =
                BinaryOpInt(int_type, *lhs_val, op, *rhs_val, error);
            if (error.Fail())
              return CreateValueObject(std::move(error));
            return CreateValueObject(lhs_type_system->GetInt(int_type, result));
          }
          break;
        }
        break;
      case TK::LeftArrowLeftArrow: {
        ZigIntType *lhs_int_type = llvm::cast<ZigIntType>(lhs_zig_type);
        rhs = rhs->Cast(rhs_type_system->GetBuiltinTypeForEncodingAndBitSize(
            eEncodingUint, lhs_int_type->GetBitSize()
                               ? llvm::Log2_32_Ceil(lhs_int_type->GetBitSize())
                               : 0));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        auto lhs_val = lhs->GetValueAsAPSInt();
        if (!lhs_val)
          return CreateValueObject(lhs_val.takeError());
        auto rhs_val = rhs->GetValueAsAPSInt();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        if (rhs_val->isZero())
          return lhs;
        if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
          return CreateValueObject(
              lhs_type_system->GetInt(lhs_int_type, *lhs_val << *shift_amt));
        break;
      }
      case TK::RightArrowRightArrow: {
        ZigIntType *lhs_int_type = llvm::cast<ZigIntType>(lhs_zig_type);
        rhs = rhs->Cast(rhs_type_system->GetBuiltinTypeForEncodingAndBitSize(
            eEncodingUint, lhs_int_type->GetBitSize()
                               ? llvm::Log2_32_Ceil(lhs_int_type->GetBitSize())
                               : 0));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        auto lhs_val = lhs->GetValueAsAPSInt();
        if (!lhs_val)
          return CreateValueObject(lhs_val.takeError());
        auto rhs_val = rhs->GetValueAsAPSInt();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        if (rhs_val->isZero())
          return lhs;
        if (std::optional<int64_t> shift_amt = rhs_val->tryExtValue())
          return CreateValueObject(
              lhs_type_system->GetInt(lhs_int_type, *lhs_val >> *shift_amt));
        break;
      }
      case TK::LeftArrowLeftArrowPipe: {
        ZigIntType *lhs_int_type = llvm::cast<ZigIntType>(lhs_zig_type);
        rhs = rhs->Cast(rhs_type_system->GetBuiltinTypeForEncodingAndBitSize(
            eEncodingUint, lhs_int_type->GetBitSize()
                               ? llvm::Log2_32_Ceil(lhs_int_type->GetBitSize())
                               : 0));
        if (!rhs || rhs->GetError().Fail())
          return rhs;
        auto lhs_val = lhs->GetValueAsAPSInt();
        if (!lhs_val)
          return CreateValueObject(lhs_val.takeError());
        auto rhs_val = rhs->GetValueAsAPSInt();
        if (!rhs_val)
          return CreateValueObject(rhs_val.takeError());
        if (rhs_val->isZero())
          return lhs;
        return CreateValueObject(
            lhs_type_system->GetInt(lhs_int_type, lhs_val->shl_sat(*rhs_val)));
      }
      }
      break;
    case ZigValue::Kind::PointerType:
      switch (rhs_zig_type->GetKind()) {
      default:
        break;
      case ZigValue::Kind::ComptimeIntType:
      case ZigValue::Kind::IntType: {
        ZigPointerType *lhs_ptr_type = llvm::cast<ZigPointerType>(lhs_zig_type);
        ZigPointerType::Size lhs_ptr_size = lhs_ptr_type->GetSize();
        switch (lhs_ptr_size) {
        case ZigPointerType::Size::One:
        case ZigPointerType::Size::Slice:
          break;
        case ZigPointerType::Size::Many:
        case ZigPointerType::Size::C: {
          addr_t lhs_ptr_addr = lhs->GetPointerValue();
          if (lhs_ptr_addr == LLDB_INVALID_ADDRESS)
            return nullptr;
          bool success;
          auto rhs_val = rhs->GetValueAsUnsigned(0, &success);
          if (!success)
            return nullptr;
          ZigType *lhs_child_type = lhs_ptr_type->GetChildType();
          uint64_t offset = lhs_child_type->GetByteSize() * rhs_val;
          CompilerType res_type = lhs_type_system->GetPointerType(
              lhs_ptr_size, lhs_ptr_type->GetSentinel(),
              lhs_ptr_type->IsAllowZero(),
              commonAlignment(lhs_ptr_type->GetPointerAlign(), offset),
              lhs_ptr_type->GetAddressSpace(), lhs_ptr_type->IsConst(),
              lhs_ptr_type->IsVolatile(), lhs_child_type);
          switch (op.kind) {
          default:
            break;
          case TK::Plus:
            return ValueObject::CreateValueObjectFromAddress(
                "", lhs_ptr_addr + offset, m_exe_ctx, res_type, false);
          case TK::Minus:
            return ValueObject::CreateValueObjectFromAddress(
                "", lhs_ptr_addr - offset, m_exe_ctx, res_type, false);
          }
          break;
        }
        }
        break;
      }
      }
      break;
    }
    return CreateValueObject(Status::FromErrorStringWithFormatv(
        "unimplemented {0} {1} {2}", lhs_type.GetDisplayTypeName(), op.source,
        rhs_type.GetDisplayTypeName()));
  }
  llvm::APSInt BinaryOpInt(ZigIntType *int_type, const llvm::APSInt &lhs,
                           Token op, const llvm::APSInt &rhs, Status &error) {
    uint64_t bits = int_type->GetBitSize();
    assert(bits == lhs.getBitWidth() && bits == rhs.getBitWidth() &&
           "wrong bit width for type");
    assert(int_type->IsSigned() == lhs.isSigned() &&
           int_type->IsSigned() == rhs.isSigned() &&
           "wrong signedness for type");
    error.Clear();
    bool overflow = false;
    switch (op.kind) {
    default:
      llvm_unreachable("not a binary op");
    case TK::Ampersand:
      return lhs & rhs;
    case TK::Caret:
      return lhs ^ rhs;
    case TK::Pipe:
      return lhs | rhs;
    case TK::Plus:
      if (llvm::APSInt res_val = rhs.isZero() ? lhs : lhs.add_ov(rhs, overflow);
          !overflow)
        return res_val;
      error = Status::FromErrorStringWithFormatv(
          "overflow of integer type '{0}' with value '{1}'",
          int_type->GetName(), lhs.extend(1 + bits) + rhs.extend(1 + bits));
      break;
    case TK::PlusPercent:
      return rhs.isZero() ? lhs : lhs + rhs;
    case TK::PlusPipe:
      return rhs.isZero() ? lhs : lhs.add_sat(rhs);
    case TK::Minus:
      if (llvm::APSInt res_val = rhs.isZero() ? lhs : lhs.sub_ov(rhs, overflow);
          !overflow)
        return res_val;
      error = Status::FromErrorStringWithFormatv(
          "overflow of integer type '{0}' with value '{1}'",
          int_type->GetName(),
          llvm::APSInt(lhs.extend(1 + bits) - rhs.extend(1 + bits), false));
      break;
    case TK::MinusPercent:
      return rhs.isZero() ? lhs : lhs - rhs;
    case TK::MinusPipe:
      return rhs.isZero() ? lhs : lhs.sub_sat(rhs);
    case TK::Asterisk:
      if (llvm::APSInt res_val = rhs.isZero() ? rhs : lhs.mul_ov(rhs, overflow);
          !overflow)
        return res_val;
      error = Status::FromErrorStringWithFormatv(
          "overflow of integer type '{0}' with value '{1}'",
          int_type->GetName(),
          lhs.extend(bits + bits) * rhs.extend(bits + bits));
      break;
    case TK::AsteriskPercent:
      return rhs.isZero() ? rhs : lhs * rhs;
    case TK::AsteriskPipe:
      return rhs.isZero() ? rhs : lhs.mul_sat(rhs);
    case TK::Slash:
      if (rhs.isZero()) {
        error = Status::FromErrorString(
            "division by zero here causes undefined behavior");
        break;
      }
      if (llvm::APSInt res_val = lhs.div_ov(rhs, overflow); !overflow)
        return res_val;
      error = Status::FromErrorStringWithFormatv(
          "overflow of integer type '{0}' with value '{1}'",
          int_type->GetName(), llvm::APSInt(lhs, true));
      break;
    case TK::Percent:
      if (rhs.isZero()) {
        error = Status::FromErrorString(
            "division by zero here causes undefined behavior");
        break;
      }
      if (lhs.isNegative() || rhs.isNegative()) {
        error = Status::FromErrorString(
            "remainder division with 'comptime_int' and 'comptime_int': "
            "signed integers and floats must use @rem or @mod");
        break;
      }
      return lhs % rhs;
    }
    return llvm::APSInt();
  }

  ExecutionContext &m_exe_ctx;
  ValueObject *m_ctx_obj;
  ZigLexer m_lexer;
  Token m_token;
  bool m_skip;
};

} // namespace

char ZigUserExpression::ID;

ZigUserExpression::ZigUserExpression(
    ExecutionContextScope &exe_scope, llvm::StringRef expr,
    llvm::StringRef prefix, SourceLanguage language, ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj)
    : UserExpression(exe_scope, expr, prefix, language, desired_type, options),
      m_ctx_obj(ctx_obj) {}

bool ZigUserExpression::Parse(DiagnosticManager &diagnostic_manager,
                              ExecutionContext &exe_ctx,
                              ExecutionPolicy execution_policy,
                              bool keep_result_in_memory,
                              bool generate_debug_info) {
  assert(m_language.AsLanguageType() == eLanguageTypeZig &&
         "ZigUserExpression only supports the Zig Language");
  return bool(m_result = ZigParser(exe_ctx, m_ctx_obj, Text()).RootExpr());
}

ExpressionResults ZigUserExpression::DoExecute(
    DiagnosticManager &diagnostic_manager, ExecutionContext &exe_ctx,
    const EvaluateExpressionOptions &options,
    UserExpressionSP &shared_ptr_to_me, ExpressionVariableSP &result) {
  if (!m_result)
    return eExpressionResultUnavailable;
  if (ValueObjectZig *zig_result = ValueObjectZig::dyn_cast(m_result.get()))
    if (auto [type_system, zig_value] = zig_result->GetZigValue();
        llvm::isa_and_present<ZigAlias>(zig_value))
      m_result = zig_result->Unwrap();
  result = std::make_shared<ZigExpressionVariable>(std::move(m_result));
  return eExpressionCompleted;
}
