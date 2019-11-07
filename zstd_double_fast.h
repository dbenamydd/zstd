/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_DOUBLE_FAST_H
#define ZSTD144_DOUBLE_FAST_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "mem.h"      /* U32 */
#include "zstd_compress_internal.h"     /* ZSTD144_CCtx, size_t */

void ZSTD144_fillDoubleHashTable(ZSTD144_matchState_t* ms,
                              void const* end, ZSTD144_dictTableLoadMethod_e dtlm);
size_t ZSTD144_compressBlock_doubleFast(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_doubleFast_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_doubleFast_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_DOUBLE_FAST_H */
