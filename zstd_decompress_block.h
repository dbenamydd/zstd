/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#ifndef ZSTD144_DEC_BLOCK_H
#define ZSTD144_DEC_BLOCK_H

/*-*******************************************************
 *  Dependencies
 *********************************************************/
#include <stddef.h>   /* size_t */
#include "zstd.h"    /* DCtx, and some public functions */
#include "zstd_internal.h"  /* blockProperties_t, and some public functions */
#include "zstd_decompress_internal.h"  /* ZSTD144_seqSymbol */


/* ===   Prototypes   === */

/* note: prototypes already published within `zstd.h` :
 * ZSTD144_decompressBlock()
 */

/* note: prototypes already published within `zstd_internal.h` :
 * ZSTD144_getcBlockSize()
 * ZSTD144_decodeSeqHeaders()
 */


/* ZSTD144_decompressBlock_internal() :
 * decompress block, starting at `src`,
 * into destination buffer `dst`.
 * @return : decompressed block size,
 *           or an error code (which can be tested using ZSTD144_isError())
 */
size_t ZSTD144_decompressBlock_internal(ZSTD144_DCtx* dctx,
                               void* dst, size_t dstCapacity,
                         const void* src, size_t srcSize, const int frame);

/* ZSTD144_buildFSETable() :
 * generate FSE decoding table for one symbol (ll, ml or off)
 * this function must be called with valid parameters only
 * (dt is large enough, normalizedCounter distribution total is a power of 2, max is within range, etc.)
 * in which case it cannot fail.
 * Internal use only.
 */
void ZSTD144_buildFSETable(ZSTD144_seqSymbol* dt,
             const short* normalizedCounter, unsigned maxSymbolValue,
             const U32* baseValue, const U32* nbAdditionalBits,
                   unsigned tableLog);


#endif /* ZSTD144_DEC_BLOCK_H */
