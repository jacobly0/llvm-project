//===-- ZigStdDataFormatters.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_LANGUAGE_ZIG_ZIGSTDDATAFORMATTERS_H
#define LLDB_PLUGINS_LANGUAGE_ZIG_ZIGSTDDATAFORMATTERS_H

#include "lldb/lldb-forward.h"

namespace lldb_private {
namespace formatters {
SyntheticChildrenFrontEnd *
ZigStdHashMapSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                      lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigStdMultiArrayListSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                             lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigStdMultiArrayListSliceSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                                  lldb::ValueObjectSP);
SyntheticChildrenFrontEnd *
ZigStdSegmentedListSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                            lldb::ValueObjectSP);
} // namespace formatters
} // namespace lldb_private

#endif // LLDB_PLUGINS_LANGUAGE_ZIG_ZIGSTDDATAFORMATTERS_H
