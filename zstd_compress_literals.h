/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_COMPRESS_LITERALS_H
#define ZSTD144_COMPRESS_LITERALS_H

#include "zstd_compress_internal.h" /* ZSTD144_hufCTables_t, ZSTD144_minGain() */


size_t ZSTD144_noCompressLiterals (void* dst, size_t dstCapacity, const void* src, size_t srcSize);

size_t ZSTD144_compressRleLiteralsBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize);

size_t ZSTD144_compressLiterals (ZSTD144_hufCTables_t const* prevHuf,
                              ZSTD144_hufCTables_t* nextHuf,
                              ZSTD144_strategy strategy, int disableLiteralCompression,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize,
                              void* entropyWorkspace, size_t entropyWorkspaceSize,
                        const int bmi2);

#endif /* ZSTD144_COMPRESS_LITERALS_H */
