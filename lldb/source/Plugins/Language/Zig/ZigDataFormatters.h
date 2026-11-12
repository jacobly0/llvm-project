//===-- ZigDataFormatters.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_LANGUAGE_ZIG_ZIGDATAFORMATTERS_H
#define LLDB_PLUGINS_LANGUAGE_ZIG_ZIGDATAFORMATTERS_H

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
bool ZigAliasSummaryProvider(ValueObject &, Stream &,
                             const TypeSummaryOptions &);
bool ZigComptimeSummaryProvider(ValueObject &, Stream &,
                                const TypeSummaryOptions &);
SyntheticChildrenFrontEnd *
ZigArrayPointerSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                        lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigSliceSyntheticFrontEndCreator(CXXSyntheticChildren *, lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigErrorUnionSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                      lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigOptionalSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                    lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigTaggedUnionSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                       lldb::ValueObjectSP);
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_PLUGINS_LANGUAGE_ZIG_ZIGDATAFORMATTERS_H
