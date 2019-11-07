/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_OPT_H
#define ZSTD144_OPT_H

#if defined (__cplusplus)
extern "C" {
#endif

#include "zstd_compress_internal.h"

/* used in ZSTD144_loadDictionaryContent() */
void ZSTD144_updateTree(ZSTD144_matchState_t* ms, const BYTE* ip, const BYTE* iend);

size_t ZSTD144_compressBlock_btopt(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_btultra(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_btultra2(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);


size_t ZSTD144_compressBlock_btopt_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_btultra_dictMatchState(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);

size_t ZSTD144_compressBlock_btopt_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);
size_t ZSTD144_compressBlock_btultra_extDict(
        ZSTD144_matchState_t* ms, seqStore_t* seqStore, U32 rep[ZSTD144_REP_NUM],
        void const* src, size_t srcSize);

        /* note : no btultra2 variant for extDict nor dictMatchState,
         * because btultra2 is not meant to work with dictionaries
         * and is only specific for the first block (no prefix) */

#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_OPT_H */
