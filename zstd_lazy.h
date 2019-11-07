/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_LAZY_H
#define ZSTD144_LAZY_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "zstd_compress_internal.h"

U32 ZSTD144_insertAndFindFirstIndex(ZSTD144_matchState_t* ms, const BYTE* ip);

void ZSTD144_preserveUnsortedMark (U32* const table, U32 const size, U32 const reducerValue);  /*! used in ZSTD144_reduceIndex(). preemptively increase value of ZSTD144_DUBT_UNSORTED_MARK */

size_t ZSTD144_compressBlock_btlazy2(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy2(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_greedy(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);

size_t ZSTD144_compressBlock_btlazy2_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy2_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_greedy_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);

size_t ZSTD144_compressBlock_greedy_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_lazy2_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_btlazy2_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);

#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_LAZY_H */
