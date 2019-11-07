/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_COMPRESS_SEQUENCES_H
#define ZSTD144_COMPRESS_SEQUENCES_H

#include "fse.h" /* FSE144_repeat, FSE144_CTable */
#include "zstd_internal.h" /* symbolEncodingType_e, ZSTD144_strategy */

typedef enum {
    ZSTD144_defaultDisallowed = 0,
    ZSTD144_defaultAllowed = 1
} ZSTD144_defaultPolicy_e;

symbolEncodingType_e
ZSTD144_selectEncodingType(
        FSE144_repeat* repeatMode, unsigned const* count, unsigned const max,
        size_t const mostFrequent, size_t nbSeq, unsigned const FSELog,
        FSE144_CTable const* prevCTable,
        short const* defaultNorm, U32 defaultNormLog,
        ZSTD144_defaultPolicy_e const isDefaultAllowed,
        ZSTD144_strategy const strategy);

size_t
ZSTD144_buildCTable(void* dst, size_t dstCapacity,
                FSE144_CTable* nextCTable, U32 FSELog, symbolEncodingType_e type,
                unsigned* count, U32 max,
                const BYTE* codeTable, size_t nbSeq,
                const S16* defaultNorm, U32 defaultNormLog, U32 defaultMax,
                const FSE144_CTable* prevCTable, size_t prevCTableSize,
                void* entropyWorkspace, size_t entropyWorkspaceSize);

size_t ZSTD144_encodeSequences(
            void* dst, size_t dstCapacity,
            FSE144_CTable const* CTable_MatchLength, BYTE const* mlCodeTable,
            FSE144_CTable const* CTable_OffsetBits, BYTE const* ofCodeTable,
            FSE144_CTable const* CTable_LitLength, BYTE const* llCodeTable,
            seqDef const* sequences, size_t nbSeq, int longOffsets, int bmi2);

#endif /* ZSTD144_COMPRESS_SEQUENCES_H */
