//===- MCSymbolOMF.h -  -----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLOMF_H
#define LLVM_MC_MCSYMBOLOMF_H

#include "llvm/MC/MCSymbol.h"

namespace llvm {
class MCSymbolOMF : public MCSymbol {
  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  MCSymbolOMF(const StringMapEntry<bool> *Name, bool isTemporary)
      : MCSymbol(SymbolKindOMF, Name, isTemporary) {}
  void setSize(const MCExpr *SS) { SymbolSize = SS; }

  const MCExpr *getSize() const { return SymbolSize; }

  static bool classof(const MCSymbol *S) { return S->isOMF(); }

private:
  void setIsBindingSet() const;
};
}

#endif
