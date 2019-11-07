/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_LEGACY_H
#define ZSTD144_LEGACY_H

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include "mem.h"            /* MEM_STATIC */
#include "error_private.h"  /* ERROR */
#include "zstd_internal.h"  /* ZSTD144_inBuffer, ZSTD144_outBuffer, ZSTD144_frameSizeInfo */

#if !defined (ZSTD144_LEGACY_SUPPORT) || (ZSTD144_LEGACY_SUPPORT == 0)
#  undef ZSTD144_LEGACY_SUPPORT
#  define ZSTD144_LEGACY_SUPPORT 8
#endif

#if (ZSTD144_LEGACY_SUPPORT <= 1)
#  include "zstd_v01.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 2)
#  include "zstd_v02.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 3)
#  include "zstd_v03.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 4)
#  include "zstd_v04.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
#  include "zstd_v05.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
#  include "zstd_v06.h"
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
#  include "zstd_v07.h"
#endif

/** ZSTD144_isLegacy() :
    @return : > 0 if supported by legacy decoder. 0 otherwise.
              return value is the version.
*/
MEM_STATIC unsigned ZSTD144_isLegacy(const void* src, size_t srcSize)
{
    U32 magicNumberLE;
    if (srcSize<4) return 0;
    magicNumberLE = MEM_readLE32(src);
    switch(magicNumberLE)
    {
#if (ZSTD144_LEGACY_SUPPORT <= 1)
        case ZSTD144v01_magicNumberLE:return 1;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 2)
        case ZSTD144v02_magicNumber : return 2;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 3)
        case ZSTD144v03_magicNumber : return 3;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case ZSTD144v04_magicNumber : return 4;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case ZSTD144v05_MAGICNUMBER : return 5;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case ZSTD144v06_MAGICNUMBER : return 6;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case ZSTD144v07_MAGICNUMBER : return 7;
#endif
        default : return 0;
    }
}


MEM_STATIC unsigned long long ZSTD144_getDecompressedSize_legacy(const void* src, size_t srcSize)
{
    U32 const version = ZSTD144_isLegacy(src, srcSize);
    if (version < 5) return 0;  /* no decompressed size in frame header, or not a legacy format */
#if (ZSTD144_LEGACY_SUPPORT <= 5)
    if (version==5) {
        ZSTD144v05_parameters fParams;
        size_t const frResult = ZSTD144v05_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.srcSize;
    }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
    if (version==6) {
        ZSTD144v06_frameParams fParams;
        size_t const frResult = ZSTD144v06_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.frameContentSize;
    }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
    if (version==7) {
        ZSTD144v07_frameParams fParams;
        size_t const frResult = ZSTD144v07_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.frameContentSize;
    }
#endif
    return 0;   /* should not be possible */
}


MEM_STATIC size_t ZSTD144_decompressLegacy(
                     void* dst, size_t dstCapacity,
               const void* src, size_t compressedSize,
               const void* dict,size_t dictSize)
{
    U32 const version = ZSTD144_isLegacy(src, compressedSize);
    (void)dst; (void)dstCapacity; (void)dict; (void)dictSize;  /* unused when ZSTD144_LEGACY_SUPPORT >= 8 */
    switch(version)
    {
#if (ZSTD144_LEGACY_SUPPORT <= 1)
        case 1 :
            return ZSTD144v01_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 2)
        case 2 :
            return ZSTD144v02_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 3)
        case 3 :
            return ZSTD144v03_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case 4 :
            return ZSTD144v04_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case 5 :
            {   size_t result;
                ZSTD144v05_DCtx* const zd = ZSTD144v05_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTD144v05_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTD144v05_freeDCtx(zd);
                return result;
            }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case 6 :
            {   size_t result;
                ZSTD144v06_DCtx* const zd = ZSTD144v06_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTD144v06_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTD144v06_freeDCtx(zd);
                return result;
            }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case 7 :
            {   size_t result;
                ZSTD144v07_DCtx* const zd = ZSTD144v07_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTD144v07_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTD144v07_freeDCtx(zd);
                return result;
            }
#endif
        default :
            return ERROR(prefix_unknown);
    }
}

MEM_STATIC ZSTD144_frameSizeInfo ZSTD144_findFrameSizeInfoLegacy(const void *src, size_t srcSize)
{
    ZSTD144_frameSizeInfo frameSizeInfo;
    U32 const version = ZSTD144_isLegacy(src, srcSize);
    switch(version)
    {
#if (ZSTD144_LEGACY_SUPPORT <= 1)
        case 1 :
            ZSTD144v01_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 2)
        case 2 :
            ZSTD144v02_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 3)
        case 3 :
            ZSTD144v03_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case 4 :
            ZSTD144v04_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case 5 :
            ZSTD144v05_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case 6 :
            ZSTD144v06_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case 7 :
            ZSTD144v07_findFrameSizeInfoLegacy(src, srcSize,
                &frameSizeInfo.compressedSize,
                &frameSizeInfo.decompressedBound);
            break;
#endif
        default :
            frameSizeInfo.compressedSize = ERROR(prefix_unknown);
            frameSizeInfo.decompressedBound = ZSTD144_CONTENTSIZE_ERROR;
            break;
    }
    if (!ZSTD144_isError(frameSizeInfo.compressedSize) && frameSizeInfo.compressedSize > srcSize) {
        frameSizeInfo.compressedSize = ERROR(srcSize_wrong);
        frameSizeInfo.decompressedBound = ZSTD144_CONTENTSIZE_ERROR;
    }
    return frameSizeInfo;
}

MEM_STATIC size_t ZSTD144_findFrameCompressedSizeLegacy(const void *src, size_t srcSize)
{
    ZSTD144_frameSizeInfo frameSizeInfo = ZSTD144_findFrameSizeInfoLegacy(src, srcSize);
    return frameSizeInfo.compressedSize;
}

MEM_STATIC size_t ZSTD144_freeLegacyStreamContext(void* legacyContext, U32 version)
{
    switch(version)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)legacyContext;
            return ERROR(version_unsupported);
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case 4 : return ZBUFF144v04_freeDCtx((ZBUFF144v04_DCtx*)legacyContext);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case 5 : return ZBUFF144v05_freeDCtx((ZBUFF144v05_DCtx*)legacyContext);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case 6 : return ZBUFF144v06_freeDCtx((ZBUFF144v06_DCtx*)legacyContext);
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case 7 : return ZBUFF144v07_freeDCtx((ZBUFF144v07_DCtx*)legacyContext);
#endif
    }
}


MEM_STATIC size_t ZSTD144_initLegacyStream(void** legacyContext, U32 prevVersion, U32 newVersion,
                                        const void* dict, size_t dictSize)
{
    DEBUGLOG(5, "ZSTD144_initLegacyStream for v0.%u", newVersion);
    if (prevVersion != newVersion) ZSTD144_freeLegacyStreamContext(*legacyContext, prevVersion);
    switch(newVersion)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)dict; (void)dictSize;
            return 0;
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case 4 :
        {
            ZBUFF144v04_DCtx* dctx = (prevVersion != newVersion) ? ZBUFF144v04_createDCtx() : (ZBUFF144v04_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFF144v04_decompressInit(dctx);
            ZBUFF144v04_decompressWithDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case 5 :
        {
            ZBUFF144v05_DCtx* dctx = (prevVersion != newVersion) ? ZBUFF144v05_createDCtx() : (ZBUFF144v05_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFF144v05_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case 6 :
        {
            ZBUFF144v06_DCtx* dctx = (prevVersion != newVersion) ? ZBUFF144v06_createDCtx() : (ZBUFF144v06_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFF144v06_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case 7 :
        {
            ZBUFF144v07_DCtx* dctx = (prevVersion != newVersion) ? ZBUFF144v07_createDCtx() : (ZBUFF144v07_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFF144v07_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
    }
}



MEM_STATIC size_t ZSTD144_decompressLegacyStream(void* legacyContext, U32 version,
                                              ZSTD144_outBuffer* output, ZSTD144_inBuffer* input)
{
    DEBUGLOG(5, "ZSTD144_decompressLegacyStream for v0.%u", version);
    switch(version)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)legacyContext; (void)output; (void)input;
            return ERROR(version_unsupported);
#if (ZSTD144_LEGACY_SUPPORT <= 4)
        case 4 :
            {
                ZBUFF144v04_DCtx* dctx = (ZBUFF144v04_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFF144v04_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 5)
        case 5 :
            {
                ZBUFF144v05_DCtx* dctx = (ZBUFF144v05_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFF144v05_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 6)
        case 6 :
            {
                ZBUFF144v06_DCtx* dctx = (ZBUFF144v06_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFF144v06_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD144_LEGACY_SUPPORT <= 7)
        case 7 :
            {
                ZBUFF144v07_DCtx* dctx = (ZBUFF144v07_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFF144v07_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
    }
}


#if defined (__cplusplus)
}
#endif

#endif   /* ZSTD144_LEGACY_H */
