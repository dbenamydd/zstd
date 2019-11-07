/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */



/* *************************************
*  Dependencies
***************************************/
#define ZBUFF144_STATIC_LINKING_ONLY
#include "zbuff.h"


ZBUFF144_DCtx* ZBUFF144_createDCtx(void)
{
    return ZSTD144_createDStream();
}

ZBUFF144_DCtx* ZBUFF144_createDCtx_advanced(ZSTD144_customMem customMem)
{
    return ZSTD144_createDStream_advanced(customMem);
}

size_t ZBUFF144_freeDCtx(ZBUFF144_DCtx* zbd)
{
    return ZSTD144_freeDStream(zbd);
}


/* *** Initialization *** */

size_t ZBUFF144_decompressInitDictionary(ZBUFF144_DCtx* zbd, const void* dict, size_t dictSize)
{
    return ZSTD144_initDStream_usingDict(zbd, dict, dictSize);
}

size_t ZBUFF144_decompressInit(ZBUFF144_DCtx* zbd)
{
    return ZSTD144_initDStream(zbd);
}


/* *** Decompression *** */

size_t ZBUFF144_decompressContinue(ZBUFF144_DCtx* zbd,
                                void* dst, size_t* dstCapacityPtr,
                          const void* src, size_t* srcSizePtr)
{
    ZSTD144_outBuffer outBuff;
    ZSTD144_inBuffer inBuff;
    size_t result;
    outBuff.dst  = dst;
    outBuff.pos  = 0;
    outBuff.size = *dstCapacityPtr;
    inBuff.src  = src;
    inBuff.pos  = 0;
    inBuff.size = *srcSizePtr;
    result = ZSTD144_decompressStream(zbd, &outBuff, &inBuff);
    *dstCapacityPtr = outBuff.pos;
    *srcSizePtr = inBuff.pos;
    return result;
}


/* *************************************
*  Tool functions
***************************************/
size_t ZBUFF144_recommendedDInSize(void)  { return ZSTD144_DStreamInSize(); }
size_t ZBUFF144_recommendedDOutSize(void) { return ZSTD144_DStreamOutSize(); }
