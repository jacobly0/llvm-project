//===- MCSectionOMF.h - OMF Machine Code Sections ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionWasm class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONOMF_H
#define LLVM_MC_MCSECTIONOMF_H

#include "llvm/MC/MCSection.h"

namespace llvm {

class MCSectionOMF final : public MCSection {
  SmallString<8> SectionName;

private:
  friend class MCContext;
  MCSectionOMF(const Twine &Section, SectionKind K, MCSymbol *Begin);

public:
  ~MCSectionOMF();

  StringRef getSectionName() const { return SectionName; }

  void PrintSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            const MCExpr *Subsection) const override;
  bool UseCodeAlign() const override { return getKind().isText(); }
  bool isVirtualSection() const override { return getKind().isBSS(); }

  static bool classof(const MCSection *S) {
    return S->getVariant() == SV_OMF;
  }
};

} // end namespace llvm

#endif
