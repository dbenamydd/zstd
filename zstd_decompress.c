/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
 * HEAPMODE :
 * Select how default decompression function ZSTD144_decompress() allocates its context,
 * on stack (0), or into heap (1, default; requires malloc()).
 * Note that functions with explicit context such as ZSTD144_decompressDCtx() are unaffected.
 */
#ifndef ZSTD144_HEAPMODE
#  define ZSTD144_HEAPMODE 1
#endif

/*!
*  LEGACY_SUPPORT :
*  if set to 1+, ZSTD144_decompress() can decode older formats (v0.1+)
*/
#ifndef ZSTD144_LEGACY_SUPPORT
#  define ZSTD144_LEGACY_SUPPORT 0
#endif

/*!
 *  MAXWINDOWSIZE_DEFAULT :
 *  maximum window size accepted by DStream __by default__.
 *  Frames requiring more memory will be rejected.
 *  It's possible to set a different limit using ZSTD144_DCtx_setMaxWindowSize().
 */
#ifndef ZSTD144_MAXWINDOWSIZE_DEFAULT
#  define ZSTD144_MAXWINDOWSIZE_DEFAULT (((U32)1 << ZSTD144_WINDOWLOG_LIMIT_DEFAULT) + 1)
#endif

/*!
 *  NO_FORWARD_PROGRESS_MAX :
 *  maximum allowed nb of calls to ZSTD144_decompressStream()
 *  without any forward progress
 *  (defined as: no byte read from input, and no byte flushed to output)
 *  before triggering an error.
 */
#ifndef ZSTD144_NO_FORWARD_PROGRESS_MAX
#  define ZSTD144_NO_FORWARD_PROGRESS_MAX 16
#endif


/*-*******************************************************
*  Dependencies
*********************************************************/
#include <string.h>      /* memcpy, memmove, memset */
#include "cpu.h"         /* bmi2 */
#include "mem.h"         /* low level memory routines */
#define FSE144_STATIC_LINKING_ONLY
#include "fse.h"
#define HUF144_STATIC_LINKING_ONLY
#include "huf.h"
#include "zstd_internal.h"  /* blockProperties_t */
#include "zstd_decompress_internal.h"   /* ZSTD144_DCtx */
#include "zstd_ddict.h"  /* ZSTD144_DDictDictContent */
#include "zstd_decompress_block.h"   /* ZSTD144_decompressBlock_internal */

#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT>=1)
#  include "zstd_legacy.h"
#endif


/*-*************************************************************
*   Context management
***************************************************************/
size_t ZSTD144_sizeof_DCtx (const ZSTD144_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support sizeof NULL */
    return sizeof(*dctx)
           + ZSTD144_sizeof_DDict(dctx->ddictLocal)
           + dctx->inBuffSize + dctx->outBuffSize;
}

size_t ZSTD144_estimateDCtxSize(void) { return sizeof(ZSTD144_DCtx); }


static size_t ZSTD144_startingInputLength(ZSTD144_format_e format)
{
    size_t const startingInputLength = ZSTD144_FRAMEHEADERSIZE_PREFIX(format);
    /* only supports formats ZSTD144_f_zstd1 and ZSTD144_f_zstd1_magicless */
    assert( (format == ZSTD144_f_zstd1) || (format == ZSTD144_f_zstd1_magicless) );
    return startingInputLength;
}

static void ZSTD144_initDCtx_internal(ZSTD144_DCtx* dctx)
{
    dctx->format = ZSTD144_f_zstd1;  /* ZSTD144_decompressBegin() invokes ZSTD144_startingInputLength() with argument dctx->format */
    dctx->staticSize  = 0;
    dctx->maxWindowSize = ZSTD144_MAXWINDOWSIZE_DEFAULT;
    dctx->ddict       = NULL;
    dctx->ddictLocal  = NULL;
    dctx->dictEnd     = NULL;
    dctx->ddictIsCold = 0;
    dctx->dictUses = ZSTD144_dont_use;
    dctx->inBuff      = NULL;
    dctx->inBuffSize  = 0;
    dctx->outBuffSize = 0;
    dctx->streamStage = zdss_init;
    dctx->legacyContext = NULL;
    dctx->previousLegacyVersion = 0;
    dctx->noForwardProgress = 0;
    dctx->bmi2 = ZSTD144_cpuid_bmi2(ZSTD144_cpuid());
}

ZSTD144_DCtx* ZSTD144_initStaticDCtx(void *workspace, size_t workspaceSize)
{
    ZSTD144_DCtx* const dctx = (ZSTD144_DCtx*) workspace;

    if ((size_t)workspace & 7) return NULL;  /* 8-aligned */
    if (workspaceSize < sizeof(ZSTD144_DCtx)) return NULL;  /* minimum size */

    ZSTD144_initDCtx_internal(dctx);
    dctx->staticSize = workspaceSize;
    dctx->inBuff = (char*)(dctx+1);
    return dctx;
}

ZSTD144_DCtx* ZSTD144_createDCtx_advanced(ZSTD144_customMem customMem)
{
    if (!customMem.customAlloc ^ !customMem.customFree) return NULL;

    {   ZSTD144_DCtx* const dctx = (ZSTD144_DCtx*)ZSTD144_malloc(sizeof(*dctx), customMem);
        if (!dctx) return NULL;
        dctx->customMem = customMem;
        ZSTD144_initDCtx_internal(dctx);
        return dctx;
    }
}

ZSTD144_DCtx* ZSTD144_createDCtx(void)
{
    DEBUGLOG(3, "ZSTD144_createDCtx");
    return ZSTD144_createDCtx_advanced(ZSTD144_defaultCMem);
}

static void ZSTD144_clearDict(ZSTD144_DCtx* dctx)
{
    ZSTD144_freeDDict(dctx->ddictLocal);
    dctx->ddictLocal = NULL;
    dctx->ddict = NULL;
    dctx->dictUses = ZSTD144_dont_use;
}

size_t ZSTD144_freeDCtx(ZSTD144_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support free on NULL */
    RETURN_ERROR_IF(dctx->staticSize, memory_allocation, "not compatible with static DCtx");
    {   ZSTD144_customMem const cMem = dctx->customMem;
        ZSTD144_clearDict(dctx);
        ZSTD144_free(dctx->inBuff, cMem);
        dctx->inBuff = NULL;
#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT >= 1)
        if (dctx->legacyContext)
            ZSTD144_freeLegacyStreamContext(dctx->legacyContext, dctx->previousLegacyVersion);
#endif
        ZSTD144_free(dctx, cMem);
        return 0;
    }
}

/* no longer useful */
void ZSTD144_copyDCtx(ZSTD144_DCtx* dstDCtx, const ZSTD144_DCtx* srcDCtx)
{
    size_t const toCopy = (size_t)((char*)(&dstDCtx->inBuff) - (char*)dstDCtx);
    memcpy(dstDCtx, srcDCtx, toCopy);  /* no need to copy workspace */
}


/*-*************************************************************
 *   Frame header decoding
 ***************************************************************/

/*! ZSTD144_isFrame() :
 *  Tells if the content of `buffer` starts with a valid Frame Identifier.
 *  Note : Frame Identifier is 4 bytes. If `size < 4`, @return will always be 0.
 *  Note 2 : Legacy Frame Identifiers are considered valid only if Legacy Support is enabled.
 *  Note 3 : Skippable Frame Identifiers are considered valid. */
unsigned ZSTD144_isFrame(const void* buffer, size_t size)
{
    if (size < ZSTD144_FRAMEIDSIZE) return 0;
    {   U32 const magic = MEM_readLE32(buffer);
        if (magic == ZSTD144_MAGICNUMBER) return 1;
        if ((magic & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) return 1;
    }
#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT >= 1)
    if (ZSTD144_isLegacy(buffer, size)) return 1;
#endif
    return 0;
}

/** ZSTD144_frameHeaderSize_internal() :
 *  srcSize must be large enough to reach header size fields.
 *  note : only works for formats ZSTD144_f_zstd1 and ZSTD144_f_zstd1_magicless.
 * @return : size of the Frame Header
 *           or an error code, which can be tested with ZSTD144_isError() */
static size_t ZSTD144_frameHeaderSize_internal(const void* src, size_t srcSize, ZSTD144_format_e format)
{
    size_t const minInputSize = ZSTD144_startingInputLength(format);
    RETURN_ERROR_IF(srcSize < minInputSize, srcSize_wrong);

    {   BYTE const fhd = ((const BYTE*)src)[minInputSize-1];
        U32 const dictID= fhd & 3;
        U32 const singleSegment = (fhd >> 5) & 1;
        U32 const fcsId = fhd >> 6;
        return minInputSize + !singleSegment
             + ZSTD144_did_fieldSize[dictID] + ZSTD144_fcs_fieldSize[fcsId]
             + (singleSegment && !fcsId);
    }
}

/** ZSTD144_frameHeaderSize() :
 *  srcSize must be >= ZSTD144_frameHeaderSize_prefix.
 * @return : size of the Frame Header,
 *           or an error code (if srcSize is too small) */
size_t ZSTD144_frameHeaderSize(const void* src, size_t srcSize)
{
    return ZSTD144_frameHeaderSize_internal(src, srcSize, ZSTD144_f_zstd1);
}


/** ZSTD144_getFrameHeader_advanced() :
 *  decode Frame Header, or require larger `srcSize`.
 *  note : only works for formats ZSTD144_f_zstd1 and ZSTD144_f_zstd1_magicless
 * @return : 0, `zfhPtr` is correctly filled,
 *          >0, `srcSize` is too small, value is wanted `srcSize` amount,
 *           or an error code, which can be tested using ZSTD144_isError() */
size_t ZSTD144_getFrameHeader_advanced(ZSTD144_frameHeader* zfhPtr, const void* src, size_t srcSize, ZSTD144_format_e format)
{
    const BYTE* ip = (const BYTE*)src;
    size_t const minInputSize = ZSTD144_startingInputLength(format);

    memset(zfhPtr, 0, sizeof(*zfhPtr));   /* not strictly necessary, but static analyzer do not understand that zfhPtr is only going to be read only if return value is zero, since they are 2 different signals */
    if (srcSize < minInputSize) return minInputSize;
    RETURN_ERROR_IF(src==NULL, GENERIC, "invalid parameter");

    if ( (format != ZSTD144_f_zstd1_magicless)
      && (MEM_readLE32(src) != ZSTD144_MAGICNUMBER) ) {
        if ((MEM_readLE32(src) & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {
            /* skippable frame */
            if (srcSize < ZSTD144_SKIPPABLEHEADERSIZE)
                return ZSTD144_SKIPPABLEHEADERSIZE; /* magic number + frame length */
            memset(zfhPtr, 0, sizeof(*zfhPtr));
            zfhPtr->frameContentSize = MEM_readLE32((const char *)src + ZSTD144_FRAMEIDSIZE);
            zfhPtr->frameType = ZSTD144_skippableFrame;
            return 0;
        }
        RETURN_ERROR(prefix_unknown);
    }

    /* ensure there is enough `srcSize` to fully read/decode frame header */
    {   size_t const fhsize = ZSTD144_frameHeaderSize_internal(src, srcSize, format);
        if (srcSize < fhsize) return fhsize;
        zfhPtr->headerSize = (U32)fhsize;
    }

    {   BYTE const fhdByte = ip[minInputSize-1];
        size_t pos = minInputSize;
        U32 const dictIDSizeCode = fhdByte&3;
        U32 const checksumFlag = (fhdByte>>2)&1;
        U32 const singleSegment = (fhdByte>>5)&1;
        U32 const fcsID = fhdByte>>6;
        U64 windowSize = 0;
        U32 dictID = 0;
        U64 frameContentSize = ZSTD144_CONTENTSIZE_UNKNOWN;
        RETURN_ERROR_IF((fhdByte & 0x08) != 0, frameParameter_unsupported,
                        "reserved bits, must be zero");

        if (!singleSegment) {
            BYTE const wlByte = ip[pos++];
            U32 const windowLog = (wlByte >> 3) + ZSTD144_WINDOWLOG_ABSOLUTEMIN;
            RETURN_ERROR_IF(windowLog > ZSTD144_WINDOWLOG_MAX, frameParameter_windowTooLarge);
            windowSize = (1ULL << windowLog);
            windowSize += (windowSize >> 3) * (wlByte&7);
        }
        switch(dictIDSizeCode)
        {
            default: assert(0);  /* impossible */
            case 0 : break;
            case 1 : dictID = ip[pos]; pos++; break;
            case 2 : dictID = MEM_readLE16(ip+pos); pos+=2; break;
            case 3 : dictID = MEM_readLE32(ip+pos); pos+=4; break;
        }
        switch(fcsID)
        {
            default: assert(0);  /* impossible */
            case 0 : if (singleSegment) frameContentSize = ip[pos]; break;
            case 1 : frameContentSize = MEM_readLE16(ip+pos)+256; break;
            case 2 : frameContentSize = MEM_readLE32(ip+pos); break;
            case 3 : frameContentSize = MEM_readLE64(ip+pos); break;
        }
        if (singleSegment) windowSize = frameContentSize;

        zfhPtr->frameType = ZSTD144_frame;
        zfhPtr->frameContentSize = frameContentSize;
        zfhPtr->windowSize = windowSize;
        zfhPtr->blockSizeMax = (unsigned) MIN(windowSize, ZSTD144_BLOCKSIZE_MAX);
        zfhPtr->dictID = dictID;
        zfhPtr->checksumFlag = checksumFlag;
    }
    return 0;
}

/** ZSTD144_getFrameHeader() :
 *  decode Frame Header, or require larger `srcSize`.
 *  note : this function does not consume input, it only reads it.
 * @return : 0, `zfhPtr` is correctly filled,
 *          >0, `srcSize` is too small, value is wanted `srcSize` amount,
 *           or an error code, which can be tested using ZSTD144_isError() */
size_t ZSTD144_getFrameHeader(ZSTD144_frameHeader* zfhPtr, const void* src, size_t srcSize)
{
    return ZSTD144_getFrameHeader_advanced(zfhPtr, src, srcSize, ZSTD144_f_zstd1);
}


/** ZSTD144_getFrameContentSize() :
 *  compatible with legacy mode
 * @return : decompressed size of the single frame pointed to be `src` if known, otherwise
 *         - ZSTD144_CONTENTSIZE_UNKNOWN if the size cannot be determined
 *         - ZSTD144_CONTENTSIZE_ERROR if an error occurred (e.g. invalid magic number, srcSize too small) */
unsigned long long ZSTD144_getFrameContentSize(const void *src, size_t srcSize)
{
#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT >= 1)
    if (ZSTD144_isLegacy(src, srcSize)) {
        unsigned long long const ret = ZSTD144_getDecompressedSize_legacy(src, srcSize);
        return ret == 0 ? ZSTD144_CONTENTSIZE_UNKNOWN : ret;
    }
#endif
    {   ZSTD144_frameHeader zfh;
        if (ZSTD144_getFrameHeader(&zfh, src, srcSize) != 0)
            return ZSTD144_CONTENTSIZE_ERROR;
        if (zfh.frameType == ZSTD144_skippableFrame) {
            return 0;
        } else {
            return zfh.frameContentSize;
    }   }
}

static size_t readSkippableFrameSize(void const* src, size_t srcSize)
{
    size_t const skippableHeaderSize = ZSTD144_SKIPPABLEHEADERSIZE;
    U32 sizeU32;

    RETURN_ERROR_IF(srcSize < ZSTD144_SKIPPABLEHEADERSIZE, srcSize_wrong);

    sizeU32 = MEM_readLE32((BYTE const*)src + ZSTD144_FRAMEIDSIZE);
    RETURN_ERROR_IF((U32)(sizeU32 + ZSTD144_SKIPPABLEHEADERSIZE) < sizeU32,
                    frameParameter_unsupported);
    {
        size_t const skippableSize = skippableHeaderSize + sizeU32;
        RETURN_ERROR_IF(skippableSize > srcSize, srcSize_wrong);
        return skippableSize;
    }
}

/** ZSTD144_findDecompressedSize() :
 *  compatible with legacy mode
 *  `srcSize` must be the exact length of some number of ZSTD compressed and/or
 *      skippable frames
 *  @return : decompressed size of the frames contained */
unsigned long long ZSTD144_findDecompressedSize(const void* src, size_t srcSize)
{
    unsigned long long totalDstSize = 0;

    while (srcSize >= ZSTD144_startingInputLength(ZSTD144_f_zstd1)) {
        U32 const magicNumber = MEM_readLE32(src);

        if ((magicNumber & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {
            size_t const skippableSize = readSkippableFrameSize(src, srcSize);
            if (ZSTD144_isError(skippableSize)) {
                return ZSTD144_CONTENTSIZE_ERROR;
            }
            assert(skippableSize <= srcSize);

            src = (const BYTE *)src + skippableSize;
            srcSize -= skippableSize;
            continue;
        }

        {   unsigned long long const ret = ZSTD144_getFrameContentSize(src, srcSize);
            if (ret >= ZSTD144_CONTENTSIZE_ERROR) return ret;

            /* check for overflow */
            if (totalDstSize + ret < totalDstSize) return ZSTD144_CONTENTSIZE_ERROR;
            totalDstSize += ret;
        }
        {   size_t const frameSrcSize = ZSTD144_findFrameCompressedSize(src, srcSize);
            if (ZSTD144_isError(frameSrcSize)) {
                return ZSTD144_CONTENTSIZE_ERROR;
            }

            src = (const BYTE *)src + frameSrcSize;
            srcSize -= frameSrcSize;
        }
    }  /* while (srcSize >= ZSTD144_frameHeaderSize_prefix) */

    if (srcSize) return ZSTD144_CONTENTSIZE_ERROR;

    return totalDstSize;
}

/** ZSTD144_getDecompressedSize() :
 *  compatible with legacy mode
 * @return : decompressed size if known, 0 otherwise
             note : 0 can mean any of the following :
                   - frame content is empty
                   - decompressed size field is not present in frame header
                   - frame header unknown / not supported
                   - frame header not complete (`srcSize` too small) */
unsigned long long ZSTD144_getDecompressedSize(const void* src, size_t srcSize)
{
    unsigned long long const ret = ZSTD144_getFrameContentSize(src, srcSize);
    ZSTD144_STATIC_ASSERT(ZSTD144_CONTENTSIZE_ERROR < ZSTD144_CONTENTSIZE_UNKNOWN);
    return (ret >= ZSTD144_CONTENTSIZE_ERROR) ? 0 : ret;
}


/** ZSTD144_decodeFrameHeader() :
 * `headerSize` must be the size provided by ZSTD144_frameHeaderSize().
 * @return : 0 if success, or an error code, which can be tested using ZSTD144_isError() */
static size_t ZSTD144_decodeFrameHeader(ZSTD144_DCtx* dctx, const void* src, size_t headerSize)
{
    size_t const result = ZSTD144_getFrameHeader_advanced(&(dctx->fParams), src, headerSize, dctx->format);
    if (ZSTD144_isError(result)) return result;    /* invalid header */
    RETURN_ERROR_IF(result>0, srcSize_wrong, "headerSize too small");
#ifndef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    /* Skip the dictID check in fuzzing mode, because it makes the search
     * harder.
     */
    RETURN_ERROR_IF(dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID),
                    dictionary_wrong);
#endif
    if (dctx->fParams.checksumFlag) XXH_3264_reset(&dctx->xxhState, 0);
    return 0;
}

static ZSTD144_frameSizeInfo ZSTD144_errorFrameSizeInfo(size_t ret)
{
    ZSTD144_frameSizeInfo frameSizeInfo;
    frameSizeInfo.compressedSize = ret;
    frameSizeInfo.decompressedBound = ZSTD144_CONTENTSIZE_ERROR;
    return frameSizeInfo;
}

static ZSTD144_frameSizeInfo ZSTD144_findFrameSizeInfo(const void* src, size_t srcSize)
{
    ZSTD144_frameSizeInfo frameSizeInfo;
    memset(&frameSizeInfo, 0, sizeof(ZSTD144_frameSizeInfo));

#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT >= 1)
    if (ZSTD144_isLegacy(src, srcSize))
        return ZSTD144_findFrameSizeInfoLegacy(src, srcSize);
#endif

    if ((srcSize >= ZSTD144_SKIPPABLEHEADERSIZE)
        && (MEM_readLE32(src) & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {
        frameSizeInfo.compressedSize = readSkippableFrameSize(src, srcSize);
        assert(ZSTD144_isError(frameSizeInfo.compressedSize) ||
               frameSizeInfo.compressedSize <= srcSize);
        return frameSizeInfo;
    } else {
        const BYTE* ip = (const BYTE*)src;
        const BYTE* const ipstart = ip;
        size_t remainingSize = srcSize;
        size_t nbBlocks = 0;
        ZSTD144_frameHeader zfh;

        /* Extract Frame Header */
        {   size_t const ret = ZSTD144_getFrameHeader(&zfh, src, srcSize);
            if (ZSTD144_isError(ret))
                return ZSTD144_errorFrameSizeInfo(ret);
            if (ret > 0)
                return ZSTD144_errorFrameSizeInfo(ERROR(srcSize_wrong));
        }

        ip += zfh.headerSize;
        remainingSize -= zfh.headerSize;

        /* Iterate over each block */
        while (1) {
            blockProperties_t blockProperties;
            size_t const cBlockSize = ZSTD144_getcBlockSize(ip, remainingSize, &blockProperties);
            if (ZSTD144_isError(cBlockSize))
                return ZSTD144_errorFrameSizeInfo(cBlockSize);

            if (ZSTD144_blockHeaderSize + cBlockSize > remainingSize)
                return ZSTD144_errorFrameSizeInfo(ERROR(srcSize_wrong));

            ip += ZSTD144_blockHeaderSize + cBlockSize;
            remainingSize -= ZSTD144_blockHeaderSize + cBlockSize;
            nbBlocks++;

            if (blockProperties.lastBlock) break;
        }

        /* Final frame content checksum */
        if (zfh.checksumFlag) {
            if (remainingSize < 4)
                return ZSTD144_errorFrameSizeInfo(ERROR(srcSize_wrong));
            ip += 4;
        }

        frameSizeInfo.compressedSize = ip - ipstart;
        frameSizeInfo.decompressedBound = (zfh.frameContentSize != ZSTD144_CONTENTSIZE_UNKNOWN)
                                        ? zfh.frameContentSize
                                        : nbBlocks * zfh.blockSizeMax;
        return frameSizeInfo;
    }
}

/** ZSTD144_findFrameCompressedSize() :
 *  compatible with legacy mode
 *  `src` must point to the start of a ZSTD frame, ZSTD legacy frame, or skippable frame
 *  `srcSize` must be at least as large as the frame contained
 *  @return : the compressed size of the frame starting at `src` */
size_t ZSTD144_findFrameCompressedSize(const void *src, size_t srcSize)
{
    ZSTD144_frameSizeInfo const frameSizeInfo = ZSTD144_findFrameSizeInfo(src, srcSize);
    return frameSizeInfo.compressedSize;
}

/** ZSTD144_decompressBound() :
 *  compatible with legacy mode
 *  `src` must point to the start of a ZSTD frame or a skippeable frame
 *  `srcSize` must be at least as large as the frame contained
 *  @return : the maximum decompressed size of the compressed source
 */
unsigned long long ZSTD144_decompressBound(const void* src, size_t srcSize)
{
    unsigned long long bound = 0;
    /* Iterate over each frame */
    while (srcSize > 0) {
        ZSTD144_frameSizeInfo const frameSizeInfo = ZSTD144_findFrameSizeInfo(src, srcSize);
        size_t const compressedSize = frameSizeInfo.compressedSize;
        unsigned long long const decompressedBound = frameSizeInfo.decompressedBound;
        if (ZSTD144_isError(compressedSize) || decompressedBound == ZSTD144_CONTENTSIZE_ERROR)
            return ZSTD144_CONTENTSIZE_ERROR;
        assert(srcSize >= compressedSize);
        src = (const BYTE*)src + compressedSize;
        srcSize -= compressedSize;
        bound += decompressedBound;
    }
    return bound;
}


/*-*************************************************************
 *   Frame decoding
 ***************************************************************/


void ZSTD144_checkContinuity(ZSTD144_DCtx* dctx, const void* dst)
{
    if (dst != dctx->previousDstEnd) {   /* not contiguous */
        dctx->dictEnd = dctx->previousDstEnd;
        dctx->virtualStart = (const char*)dst - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->prefixStart));
        dctx->prefixStart = dst;
        dctx->previousDstEnd = dst;
    }
}

/** ZSTD144_insertBlock() :
 *  insert `src` block into `dctx` history. Useful to track uncompressed blocks. */
size_t ZSTD144_insertBlock(ZSTD144_DCtx* dctx, const void* blockStart, size_t blockSize)
{
    DEBUGLOG(5, "ZSTD144_insertBlock: %u bytes", (unsigned)blockSize);
    ZSTD144_checkContinuity(dctx, blockStart);
    dctx->previousDstEnd = (const char*)blockStart + blockSize;
    return blockSize;
}


static size_t ZSTD144_copyRawBlock(void* dst, size_t dstCapacity,
                          const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD144_copyRawBlock");
    if (dst == NULL) {
        if (srcSize == 0) return 0;
        RETURN_ERROR(dstBuffer_null);
    }
    RETURN_ERROR_IF(srcSize > dstCapacity, dstSize_tooSmall);
    memcpy(dst, src, srcSize);
    return srcSize;
}

static size_t ZSTD144_setRleBlock(void* dst, size_t dstCapacity,
                               BYTE b,
                               size_t regenSize)
{
    if (dst == NULL) {
        if (regenSize == 0) return 0;
        RETURN_ERROR(dstBuffer_null);
    }
    RETURN_ERROR_IF(regenSize > dstCapacity, dstSize_tooSmall);
    memset(dst, b, regenSize);
    return regenSize;
}


/*! ZSTD144_decompressFrame() :
 * @dctx must be properly initialized
 *  will update *srcPtr and *srcSizePtr,
 *  to make *srcPtr progress by one frame. */
static size_t ZSTD144_decompressFrame(ZSTD144_DCtx* dctx,
                                   void* dst, size_t dstCapacity,
                             const void** srcPtr, size_t *srcSizePtr)
{
    const BYTE* ip = (const BYTE*)(*srcPtr);
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    size_t remainingSrcSize = *srcSizePtr;

    DEBUGLOG(4, "ZSTD144_decompressFrame (srcSize:%i)", (int)*srcSizePtr);

    /* check */
    RETURN_ERROR_IF(
        remainingSrcSize < ZSTD144_FRAMEHEADERSIZE_MIN(dctx->format)+ZSTD144_blockHeaderSize,
        srcSize_wrong);

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTD144_frameHeaderSize_internal(
                ip, ZSTD144_FRAMEHEADERSIZE_PREFIX(dctx->format), dctx->format);
        if (ZSTD144_isError(frameHeaderSize)) return frameHeaderSize;
        RETURN_ERROR_IF(remainingSrcSize < frameHeaderSize+ZSTD144_blockHeaderSize,
                        srcSize_wrong);
        FORWARD_IF_ERROR( ZSTD144_decodeFrameHeader(dctx, ip, frameHeaderSize) );
        ip += frameHeaderSize; remainingSrcSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        size_t decodedSize;
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTD144_getcBlockSize(ip, remainingSrcSize, &blockProperties);
        if (ZSTD144_isError(cBlockSize)) return cBlockSize;

        ip += ZSTD144_blockHeaderSize;
        remainingSrcSize -= ZSTD144_blockHeaderSize;
        RETURN_ERROR_IF(cBlockSize > remainingSrcSize, srcSize_wrong);

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            decodedSize = ZSTD144_decompressBlock_internal(dctx, op, oend-op, ip, cBlockSize, /* frame */ 1);
            break;
        case bt_raw :
            decodedSize = ZSTD144_copyRawBlock(op, oend-op, ip, cBlockSize);
            break;
        case bt_rle :
            decodedSize = ZSTD144_setRleBlock(op, oend-op, *ip, blockProperties.origSize);
            break;
        case bt_reserved :
        default:
            RETURN_ERROR(corruption_detected);
        }

        if (ZSTD144_isError(decodedSize)) return decodedSize;
        if (dctx->fParams.checksumFlag)
            XXH_3264_update(&dctx->xxhState, op, decodedSize);
        op += decodedSize;
        ip += cBlockSize;
        remainingSrcSize -= cBlockSize;
        if (blockProperties.lastBlock) break;
    }

    if (dctx->fParams.frameContentSize != ZSTD144_CONTENTSIZE_UNKNOWN) {
        RETURN_ERROR_IF((U64)(op-ostart) != dctx->fParams.frameContentSize,
                        corruption_detected);
    }
    if (dctx->fParams.checksumFlag) { /* Frame content checksum verification */
        U32 const checkCalc = (U32)XXH_3264_digest(&dctx->xxhState);
        U32 checkRead;
        RETURN_ERROR_IF(remainingSrcSize<4, checksum_wrong);
        checkRead = MEM_readLE32(ip);
        RETURN_ERROR_IF(checkRead != checkCalc, checksum_wrong);
        ip += 4;
        remainingSrcSize -= 4;
    }

    /* Allow caller to get size read */
    *srcPtr = ip;
    *srcSizePtr = remainingSrcSize;
    return op-ostart;
}

static size_t ZSTD144_decompressMultiFrame(ZSTD144_DCtx* dctx,
                                        void* dst, size_t dstCapacity,
                                  const void* src, size_t srcSize,
                                  const void* dict, size_t dictSize,
                                  const ZSTD144_DDict* ddict)
{
    void* const dststart = dst;
    int moreThan1Frame = 0;

    DEBUGLOG(5, "ZSTD144_decompressMultiFrame");
    assert(dict==NULL || ddict==NULL);  /* either dict or ddict set, not both */

    if (ddict) {
        dict = ZSTD144_DDict_dictContent(ddict);
        dictSize = ZSTD144_DDict_dictSize(ddict);
    }

    while (srcSize >= ZSTD144_startingInputLength(dctx->format)) {

#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT >= 1)
        if (ZSTD144_isLegacy(src, srcSize)) {
            size_t decodedSize;
            size_t const frameSize = ZSTD144_findFrameCompressedSizeLegacy(src, srcSize);
            if (ZSTD144_isError(frameSize)) return frameSize;
            RETURN_ERROR_IF(dctx->staticSize, memory_allocation,
                "legacy support is not compatible with static dctx");

            decodedSize = ZSTD144_decompressLegacy(dst, dstCapacity, src, frameSize, dict, dictSize);
            if (ZSTD144_isError(decodedSize)) return decodedSize;

            assert(decodedSize <=- dstCapacity);
            dst = (BYTE*)dst + decodedSize;
            dstCapacity -= decodedSize;

            src = (const BYTE*)src + frameSize;
            srcSize -= frameSize;

            continue;
        }
#endif

        {   U32 const magicNumber = MEM_readLE32(src);
            DEBUGLOG(4, "reading magic number %08X (expecting %08X)",
                        (unsigned)magicNumber, ZSTD144_MAGICNUMBER);
            if ((magicNumber & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {
                size_t const skippableSize = readSkippableFrameSize(src, srcSize);
                FORWARD_IF_ERROR(skippableSize);
                assert(skippableSize <= srcSize);

                src = (const BYTE *)src + skippableSize;
                srcSize -= skippableSize;
                continue;
        }   }

        if (ddict) {
            /* we were called from ZSTD144_decompress_usingDDict */
            FORWARD_IF_ERROR(ZSTD144_decompressBegin_usingDDict(dctx, ddict));
        } else {
            /* this will initialize correctly with no dict if dict == NULL, so
             * use this in all cases but ddict */
            FORWARD_IF_ERROR(ZSTD144_decompressBegin_usingDict(dctx, dict, dictSize));
        }
        ZSTD144_checkContinuity(dctx, dst);

        {   const size_t res = ZSTD144_decompressFrame(dctx, dst, dstCapacity,
                                                    &src, &srcSize);
            RETURN_ERROR_IF(
                (ZSTD144_getErrorCode(res) == ZSTD144_error_prefix_unknown)
             && (moreThan1Frame==1),
                srcSize_wrong,
                "at least one frame successfully completed, but following "
                "bytes are garbage: it's more likely to be a srcSize error, "
                "specifying more bytes than compressed size of frame(s). This "
                "error message replaces ERROR(prefix_unknown), which would be "
                "confusing, as the first header is actually correct. Note that "
                "one could be unlucky, it might be a corruption error instead, "
                "happening right at the place where we expect zstd magic "
                "bytes. But this is _much_ less likely than a srcSize field "
                "error.");
            if (ZSTD144_isError(res)) return res;
            assert(res <= dstCapacity);
            dst = (BYTE*)dst + res;
            dstCapacity -= res;
        }
        moreThan1Frame = 1;
    }  /* while (srcSize >= ZSTD144_frameHeaderSize_prefix) */

    RETURN_ERROR_IF(srcSize, srcSize_wrong, "input not entirely consumed");

    return (BYTE*)dst - (BYTE*)dststart;
}

size_t ZSTD144_decompress_usingDict(ZSTD144_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                           const void* src, size_t srcSize,
                           const void* dict, size_t dictSize)
{
    return ZSTD144_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize, dict, dictSize, NULL);
}


static ZSTD144_DDict const* ZSTD144_getDDict(ZSTD144_DCtx* dctx)
{
    switch (dctx->dictUses) {
    default:
        assert(0 /* Impossible */);
        /* fall-through */
    case ZSTD144_dont_use:
        ZSTD144_clearDict(dctx);
        return NULL;
    case ZSTD144_use_indefinitely:
        return dctx->ddict;
    case ZSTD144_use_once:
        dctx->dictUses = ZSTD144_dont_use;
        return dctx->ddict;
    }
}

size_t ZSTD144_decompressDCtx(ZSTD144_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return ZSTD144_decompress_usingDDict(dctx, dst, dstCapacity, src, srcSize, ZSTD144_getDDict(dctx));
}


size_t ZSTD144_decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
#if defined(ZSTD144_HEAPMODE) && (ZSTD144_HEAPMODE>=1)
    size_t regenSize;
    ZSTD144_DCtx* const dctx = ZSTD144_createDCtx();
    RETURN_ERROR_IF(dctx==NULL, memory_allocation);
    regenSize = ZSTD144_decompressDCtx(dctx, dst, dstCapacity, src, srcSize);
    ZSTD144_freeDCtx(dctx);
    return regenSize;
#else   /* stack mode */
    ZSTD144_DCtx dctx;
    ZSTD144_initDCtx_internal(&dctx);
    return ZSTD144_decompressDCtx(&dctx, dst, dstCapacity, src, srcSize);
#endif
}


/*-**************************************
*   Advanced Streaming Decompression API
*   Bufferless and synchronous
****************************************/
size_t ZSTD144_nextSrcSizeToDecompress(ZSTD144_DCtx* dctx) { return dctx->expected; }

ZSTD144_nextInputType_e ZSTD144_nextInputType(ZSTD144_DCtx* dctx) {
    switch(dctx->stage)
    {
    default:   /* should not happen */
        assert(0);
    case ZSTDds_getFrameHeaderSize:
    case ZSTDds_decodeFrameHeader:
        return ZSTDnit_frameHeader;
    case ZSTDds_decodeBlockHeader:
        return ZSTDnit_blockHeader;
    case ZSTDds_decompressBlock:
        return ZSTDnit_block;
    case ZSTDds_decompressLastBlock:
        return ZSTDnit_lastBlock;
    case ZSTDds_checkChecksum:
        return ZSTDnit_checksum;
    case ZSTDds_decodeSkippableHeader:
    case ZSTDds_skipFrame:
        return ZSTDnit_skippableFrame;
    }
}

static int ZSTD144_isSkipFrame(ZSTD144_DCtx* dctx) { return dctx->stage == ZSTDds_skipFrame; }

/** ZSTD144_decompressContinue() :
 *  srcSize : must be the exact nb of bytes expected (see ZSTD144_nextSrcSizeToDecompress())
 *  @return : nb of bytes generated into `dst` (necessarily <= `dstCapacity)
 *            or an error code, which can be tested using ZSTD144_isError() */
size_t ZSTD144_decompressContinue(ZSTD144_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD144_decompressContinue (srcSize:%u)", (unsigned)srcSize);
    /* Sanity check */
    RETURN_ERROR_IF(srcSize != dctx->expected, srcSize_wrong, "not allowed");
    if (dstCapacity) ZSTD144_checkContinuity(dctx, dst);

    switch (dctx->stage)
    {
    case ZSTDds_getFrameHeaderSize :
        assert(src != NULL);
        if (dctx->format == ZSTD144_f_zstd1) {  /* allows header */
            assert(srcSize >= ZSTD144_FRAMEIDSIZE);  /* to read skippable magic number */
            if ((MEM_readLE32(src) & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {        /* skippable frame */
                memcpy(dctx->headerBuffer, src, srcSize);
                dctx->expected = ZSTD144_SKIPPABLEHEADERSIZE - srcSize;  /* remaining to load to get full skippable frame header */
                dctx->stage = ZSTDds_decodeSkippableHeader;
                return 0;
        }   }
        dctx->headerSize = ZSTD144_frameHeaderSize_internal(src, srcSize, dctx->format);
        if (ZSTD144_isError(dctx->headerSize)) return dctx->headerSize;
        memcpy(dctx->headerBuffer, src, srcSize);
        dctx->expected = dctx->headerSize - srcSize;
        dctx->stage = ZSTDds_decodeFrameHeader;
        return 0;

    case ZSTDds_decodeFrameHeader:
        assert(src != NULL);
        memcpy(dctx->headerBuffer + (dctx->headerSize - srcSize), src, srcSize);
        FORWARD_IF_ERROR(ZSTD144_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize));
        dctx->expected = ZSTD144_blockHeaderSize;
        dctx->stage = ZSTDds_decodeBlockHeader;
        return 0;

    case ZSTDds_decodeBlockHeader:
        {   blockProperties_t bp;
            size_t const cBlockSize = ZSTD144_getcBlockSize(src, ZSTD144_blockHeaderSize, &bp);
            if (ZSTD144_isError(cBlockSize)) return cBlockSize;
            RETURN_ERROR_IF(cBlockSize > dctx->fParams.blockSizeMax, corruption_detected, "Block Size Exceeds Maximum");
            dctx->expected = cBlockSize;
            dctx->bType = bp.blockType;
            dctx->rleSize = bp.origSize;
            if (cBlockSize) {
                dctx->stage = bp.lastBlock ? ZSTDds_decompressLastBlock : ZSTDds_decompressBlock;
                return 0;
            }
            /* empty block */
            if (bp.lastBlock) {
                if (dctx->fParams.checksumFlag) {
                    dctx->expected = 4;
                    dctx->stage = ZSTDds_checkChecksum;
                } else {
                    dctx->expected = 0; /* end of frame */
                    dctx->stage = ZSTDds_getFrameHeaderSize;
                }
            } else {
                dctx->expected = ZSTD144_blockHeaderSize;  /* jump to next header */
                dctx->stage = ZSTDds_decodeBlockHeader;
            }
            return 0;
        }

    case ZSTDds_decompressLastBlock:
    case ZSTDds_decompressBlock:
        DEBUGLOG(5, "ZSTD144_decompressContinue: case ZSTDds_decompressBlock");
        {   size_t rSize;
            switch(dctx->bType)
            {
            case bt_compressed:
                DEBUGLOG(5, "ZSTD144_decompressContinue: case bt_compressed");
                rSize = ZSTD144_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize, /* frame */ 1);
                break;
            case bt_raw :
                rSize = ZSTD144_copyRawBlock(dst, dstCapacity, src, srcSize);
                break;
            case bt_rle :
                rSize = ZSTD144_setRleBlock(dst, dstCapacity, *(const BYTE*)src, dctx->rleSize);
                break;
            case bt_reserved :   /* should never happen */
            default:
                RETURN_ERROR(corruption_detected);
            }
            if (ZSTD144_isError(rSize)) return rSize;
            RETURN_ERROR_IF(rSize > dctx->fParams.blockSizeMax, corruption_detected, "Decompressed Block Size Exceeds Maximum");
            DEBUGLOG(5, "ZSTD144_decompressContinue: decoded size from block : %u", (unsigned)rSize);
            dctx->decodedSize += rSize;
            if (dctx->fParams.checksumFlag) XXH_3264_update(&dctx->xxhState, dst, rSize);

            if (dctx->stage == ZSTDds_decompressLastBlock) {   /* end of frame */
                DEBUGLOG(4, "ZSTD144_decompressContinue: decoded size from frame : %u", (unsigned)dctx->decodedSize);
                RETURN_ERROR_IF(
                    dctx->fParams.frameContentSize != ZSTD144_CONTENTSIZE_UNKNOWN
                 && dctx->decodedSize != dctx->fParams.frameContentSize,
                    corruption_detected);
                if (dctx->fParams.checksumFlag) {  /* another round for frame checksum */
                    dctx->expected = 4;
                    dctx->stage = ZSTDds_checkChecksum;
                } else {
                    dctx->expected = 0;   /* ends here */
                    dctx->stage = ZSTDds_getFrameHeaderSize;
                }
            } else {
                dctx->stage = ZSTDds_decodeBlockHeader;
                dctx->expected = ZSTD144_blockHeaderSize;
                dctx->previousDstEnd = (char*)dst + rSize;
            }
            return rSize;
        }

    case ZSTDds_checkChecksum:
        assert(srcSize == 4);  /* guaranteed by dctx->expected */
        {   U32 const h32 = (U32)XXH_3264_digest(&dctx->xxhState);
            U32 const check32 = MEM_readLE32(src);
            DEBUGLOG(4, "ZSTD144_decompressContinue: checksum : calculated %08X :: %08X read", (unsigned)h32, (unsigned)check32);
            RETURN_ERROR_IF(check32 != h32, checksum_wrong);
            dctx->expected = 0;
            dctx->stage = ZSTDds_getFrameHeaderSize;
            return 0;
        }

    case ZSTDds_decodeSkippableHeader:
        assert(src != NULL);
        assert(srcSize <= ZSTD144_SKIPPABLEHEADERSIZE);
        memcpy(dctx->headerBuffer + (ZSTD144_SKIPPABLEHEADERSIZE - srcSize), src, srcSize);   /* complete skippable header */
        dctx->expected = MEM_readLE32(dctx->headerBuffer + ZSTD144_FRAMEIDSIZE);   /* note : dctx->expected can grow seriously large, beyond local buffer size */
        dctx->stage = ZSTDds_skipFrame;
        return 0;

    case ZSTDds_skipFrame:
        dctx->expected = 0;
        dctx->stage = ZSTDds_getFrameHeaderSize;
        return 0;

    default:
        assert(0);   /* impossible */
        RETURN_ERROR(GENERIC);   /* some compiler require default to do something */
    }
}


static size_t ZSTD144_refDictContent(ZSTD144_DCtx* dctx, const void* dict, size_t dictSize)
{
    dctx->dictEnd = dctx->previousDstEnd;
    dctx->virtualStart = (const char*)dict - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->prefixStart));
    dctx->prefixStart = dict;
    dctx->previousDstEnd = (const char*)dict + dictSize;
    return 0;
}

/*! ZSTD144_loadDEntropy() :
 *  dict : must point at beginning of a valid zstd dictionary.
 * @return : size of entropy tables read */
size_t
ZSTD144_loadDEntropy(ZSTD144_entropyDTables_t* entropy,
                  const void* const dict, size_t const dictSize)
{
    const BYTE* dictPtr = (const BYTE*)dict;
    const BYTE* const dictEnd = dictPtr + dictSize;

    RETURN_ERROR_IF(dictSize <= 8, dictionary_corrupted);
    assert(MEM_readLE32(dict) == ZSTD144_MAGIC_DICTIONARY);   /* dict must be valid */
    dictPtr += 8;   /* skip header = magic + dictID */

    ZSTD144_STATIC_ASSERT(offsetof(ZSTD144_entropyDTables_t, OFTable) == offsetof(ZSTD144_entropyDTables_t, LLTable) + sizeof(entropy->LLTable));
    ZSTD144_STATIC_ASSERT(offsetof(ZSTD144_entropyDTables_t, MLTable) == offsetof(ZSTD144_entropyDTables_t, OFTable) + sizeof(entropy->OFTable));
    ZSTD144_STATIC_ASSERT(sizeof(entropy->LLTable) + sizeof(entropy->OFTable) + sizeof(entropy->MLTable) >= HUF144_DECOMPRESS_WORKSPACE_SIZE);
    {   void* const workspace = &entropy->LLTable;   /* use fse tables as temporary workspace; implies fse tables are grouped together */
        size_t const workspaceSize = sizeof(entropy->LLTable) + sizeof(entropy->OFTable) + sizeof(entropy->MLTable);
#ifdef HUF144_FORCE_DECOMPRESS_X1
        /* in minimal huffman, we always use X1 variants */
        size_t const hSize = HUF144_readDTableX1_wksp(entropy->hufTable,
                                                dictPtr, dictEnd - dictPtr,
                                                workspace, workspaceSize);
#else
        size_t const hSize = HUF144_readDTableX2_wksp(entropy->hufTable,
                                                dictPtr, dictEnd - dictPtr,
                                                workspace, workspaceSize);
#endif
        RETURN_ERROR_IF(HUF144_isError(hSize), dictionary_corrupted);
        dictPtr += hSize;
    }

    {   short offcodeNCount[MaxOff+1];
        unsigned offcodeMaxValue = MaxOff, offcodeLog;
        size_t const offcodeHeaderSize = FSE144_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(offcodeHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(offcodeMaxValue > MaxOff, dictionary_corrupted);
        RETURN_ERROR_IF(offcodeLog > OffFSELog, dictionary_corrupted);
        ZSTD144_buildFSETable( entropy->OFTable,
                            offcodeNCount, offcodeMaxValue,
                            OF144_base, OF144_bits,
                            offcodeLog);
        dictPtr += offcodeHeaderSize;
    }

    {   short matchlengthNCount[MaxML+1];
        unsigned matchlengthMaxValue = MaxML, matchlengthLog;
        size_t const matchlengthHeaderSize = FSE144_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(matchlengthHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(matchlengthMaxValue > MaxML, dictionary_corrupted);
        RETURN_ERROR_IF(matchlengthLog > MLFSELog, dictionary_corrupted);
        ZSTD144_buildFSETable( entropy->MLTable,
                            matchlengthNCount, matchlengthMaxValue,
                            ML144_base, ML144_bits,
                            matchlengthLog);
        dictPtr += matchlengthHeaderSize;
    }

    {   short litlengthNCount[MaxLL+1];
        unsigned litlengthMaxValue = MaxLL, litlengthLog;
        size_t const litlengthHeaderSize = FSE144_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(litlengthHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(litlengthMaxValue > MaxLL, dictionary_corrupted);
        RETURN_ERROR_IF(litlengthLog > LLFSELog, dictionary_corrupted);
        ZSTD144_buildFSETable( entropy->LLTable,
                            litlengthNCount, litlengthMaxValue,
                            LL144_base, LL144_bits,
                            litlengthLog);
        dictPtr += litlengthHeaderSize;
    }

    RETURN_ERROR_IF(dictPtr+12 > dictEnd, dictionary_corrupted);
    {   int i;
        size_t const dictContentSize = (size_t)(dictEnd - (dictPtr+12));
        for (i=0; i<3; i++) {
            U32 const rep = MEM_readLE32(dictPtr); dictPtr += 4;
            RETURN_ERROR_IF(rep==0 || rep > dictContentSize,
                            dictionary_corrupted);
            entropy->rep[i] = rep;
    }   }

    return dictPtr - (const BYTE*)dict;
}

static size_t ZSTD144_decompress_insertDictionary(ZSTD144_DCtx* dctx, const void* dict, size_t dictSize)
{
    if (dictSize < 8) return ZSTD144_refDictContent(dctx, dict, dictSize);
    {   U32 const magic = MEM_readLE32(dict);
        if (magic != ZSTD144_MAGIC_DICTIONARY) {
            return ZSTD144_refDictContent(dctx, dict, dictSize);   /* pure content mode */
    }   }
    dctx->dictID = MEM_readLE32((const char*)dict + ZSTD144_FRAMEIDSIZE);

    /* load entropy tables */
    {   size_t const eSize = ZSTD144_loadDEntropy(&dctx->entropy, dict, dictSize);
        RETURN_ERROR_IF(ZSTD144_isError(eSize), dictionary_corrupted);
        dict = (const char*)dict + eSize;
        dictSize -= eSize;
    }
    dctx->litEntropy = dctx->fseEntropy = 1;

    /* reference dictionary content */
    return ZSTD144_refDictContent(dctx, dict, dictSize);
}

size_t ZSTD144_decompressBegin(ZSTD144_DCtx* dctx)
{
    assert(dctx != NULL);
    dctx->expected = ZSTD144_startingInputLength(dctx->format);  /* dctx->format must be properly set */
    dctx->stage = ZSTDds_getFrameHeaderSize;
    dctx->decodedSize = 0;
    dctx->previousDstEnd = NULL;
    dctx->prefixStart = NULL;
    dctx->virtualStart = NULL;
    dctx->dictEnd = NULL;
    dctx->entropy.hufTable[0] = (HUF144_DTable)((HufLog)*0x1000001);  /* cover both little and big endian */
    dctx->litEntropy = dctx->fseEntropy = 0;
    dctx->dictID = 0;
    ZSTD144_STATIC_ASSERT(sizeof(dctx->entropy.rep) == sizeof(repStartValue));
    memcpy(dctx->entropy.rep, repStartValue, sizeof(repStartValue));  /* initial repcodes */
    dctx->LLTptr = dctx->entropy.LLTable;
    dctx->MLTptr = dctx->entropy.MLTable;
    dctx->OFTptr = dctx->entropy.OFTable;
    dctx->HUFptr = dctx->entropy.hufTable;
    return 0;
}

size_t ZSTD144_decompressBegin_usingDict(ZSTD144_DCtx* dctx, const void* dict, size_t dictSize)
{
    FORWARD_IF_ERROR( ZSTD144_decompressBegin(dctx) );
    if (dict && dictSize)
        RETURN_ERROR_IF(
            ZSTD144_isError(ZSTD144_decompress_insertDictionary(dctx, dict, dictSize)),
            dictionary_corrupted);
    return 0;
}


/* ======   ZSTD144_DDict   ====== */

size_t ZSTD144_decompressBegin_usingDDict(ZSTD144_DCtx* dctx, const ZSTD144_DDict* ddict)
{
    DEBUGLOG(4, "ZSTD144_decompressBegin_usingDDict");
    assert(dctx != NULL);
    if (ddict) {
        const char* const dictStart = (const char*)ZSTD144_DDict_dictContent(ddict);
        size_t const dictSize = ZSTD144_DDict_dictSize(ddict);
        const void* const dictEnd = dictStart + dictSize;
        dctx->ddictIsCold = (dctx->dictEnd != dictEnd);
        DEBUGLOG(4, "DDict is %s",
                    dctx->ddictIsCold ? "~cold~" : "hot!");
    }
    FORWARD_IF_ERROR( ZSTD144_decompressBegin(dctx) );
    if (ddict) {   /* NULL ddict is equivalent to no dictionary */
        ZSTD144_copyDDictParameters(dctx, ddict);
    }
    return 0;
}

/*! ZSTD144_getDictID_fromDict() :
 *  Provides the dictID stored within dictionary.
 *  if @return == 0, the dictionary is not conformant with Zstandard specification.
 *  It can still be loaded, but as a content-only dictionary. */
unsigned ZSTD144_getDictID_fromDict(const void* dict, size_t dictSize)
{
    if (dictSize < 8) return 0;
    if (MEM_readLE32(dict) != ZSTD144_MAGIC_DICTIONARY) return 0;
    return MEM_readLE32((const char*)dict + ZSTD144_FRAMEIDSIZE);
}

/*! ZSTD144_getDictID_fromFrame() :
 *  Provides the dictID required to decompress frame stored within `src`.
 *  If @return == 0, the dictID could not be decoded.
 *  This could for one of the following reasons :
 *  - The frame does not require a dictionary (most common case).
 *  - The frame was built with dictID intentionally removed.
 *    Needed dictionary is a hidden information.
 *    Note : this use case also happens when using a non-conformant dictionary.
 *  - `srcSize` is too small, and as a result, frame header could not be decoded.
 *    Note : possible if `srcSize < ZSTD144_FRAMEHEADERSIZE_MAX`.
 *  - This is not a Zstandard frame.
 *  When identifying the exact failure cause, it's possible to use
 *  ZSTD144_getFrameHeader(), which will provide a more precise error code. */
unsigned ZSTD144_getDictID_fromFrame(const void* src, size_t srcSize)
{
    ZSTD144_frameHeader zfp = { 0, 0, 0, ZSTD144_frame, 0, 0, 0 };
    size_t const hError = ZSTD144_getFrameHeader(&zfp, src, srcSize);
    if (ZSTD144_isError(hError)) return 0;
    return zfp.dictID;
}


/*! ZSTD144_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Use dictionary without significant overhead. */
size_t ZSTD144_decompress_usingDDict(ZSTD144_DCtx* dctx,
                                  void* dst, size_t dstCapacity,
                            const void* src, size_t srcSize,
                            const ZSTD144_DDict* ddict)
{
    /* pass content and size in case legacy frames are encountered */
    return ZSTD144_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize,
                                     NULL, 0,
                                     ddict);
}


/*=====================================
*   Streaming decompression
*====================================*/

ZSTD144_DStream* ZSTD144_createDStream(void)
{
    DEBUGLOG(3, "ZSTD144_createDStream");
    return ZSTD144_createDStream_advanced(ZSTD144_defaultCMem);
}

ZSTD144_DStream* ZSTD144_initStaticDStream(void *workspace, size_t workspaceSize)
{
    return ZSTD144_initStaticDCtx(workspace, workspaceSize);
}

ZSTD144_DStream* ZSTD144_createDStream_advanced(ZSTD144_customMem customMem)
{
    return ZSTD144_createDCtx_advanced(customMem);
}

size_t ZSTD144_freeDStream(ZSTD144_DStream* zds)
{
    return ZSTD144_freeDCtx(zds);
}


/* ***  Initialization  *** */

size_t ZSTD144_DStreamInSize(void)  { return ZSTD144_BLOCKSIZE_MAX + ZSTD144_blockHeaderSize; }
size_t ZSTD144_DStreamOutSize(void) { return ZSTD144_BLOCKSIZE_MAX; }

size_t ZSTD144_DCtx_loadDictionary_advanced(ZSTD144_DCtx* dctx,
                                   const void* dict, size_t dictSize,
                                         ZSTD144_dictLoadMethod_e dictLoadMethod,
                                         ZSTD144_dictContentType_e dictContentType)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong);
    ZSTD144_clearDict(dctx);
    if (dict && dictSize != 0) {
        dctx->ddictLocal = ZSTD144_createDDict_advanced(dict, dictSize, dictLoadMethod, dictContentType, dctx->customMem);
        RETURN_ERROR_IF(dctx->ddictLocal == NULL, memory_allocation);
        dctx->ddict = dctx->ddictLocal;
        dctx->dictUses = ZSTD144_use_indefinitely;
    }
    return 0;
}

size_t ZSTD144_DCtx_loadDictionary_byReference(ZSTD144_DCtx* dctx, const void* dict, size_t dictSize)
{
    return ZSTD144_DCtx_loadDictionary_advanced(dctx, dict, dictSize, ZSTD144_dlm_byRef, ZSTD144_dct_auto);
}

size_t ZSTD144_DCtx_loadDictionary(ZSTD144_DCtx* dctx, const void* dict, size_t dictSize)
{
    return ZSTD144_DCtx_loadDictionary_advanced(dctx, dict, dictSize, ZSTD144_dlm_byCopy, ZSTD144_dct_auto);
}

size_t ZSTD144_DCtx_refPrefix_advanced(ZSTD144_DCtx* dctx, const void* prefix, size_t prefixSize, ZSTD144_dictContentType_e dictContentType)
{
    FORWARD_IF_ERROR(ZSTD144_DCtx_loadDictionary_advanced(dctx, prefix, prefixSize, ZSTD144_dlm_byRef, dictContentType));
    dctx->dictUses = ZSTD144_use_once;
    return 0;
}

size_t ZSTD144_DCtx_refPrefix(ZSTD144_DCtx* dctx, const void* prefix, size_t prefixSize)
{
    return ZSTD144_DCtx_refPrefix_advanced(dctx, prefix, prefixSize, ZSTD144_dct_rawContent);
}


/* ZSTD144_initDStream_usingDict() :
 * return : expected size, aka ZSTD144_startingInputLength().
 * this function cannot fail */
size_t ZSTD144_initDStream_usingDict(ZSTD144_DStream* zds, const void* dict, size_t dictSize)
{
    DEBUGLOG(4, "ZSTD144_initDStream_usingDict");
    FORWARD_IF_ERROR( ZSTD144_DCtx_reset(zds, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_DCtx_loadDictionary(zds, dict, dictSize) );
    return ZSTD144_startingInputLength(zds->format);
}

/* note : this variant can't fail */
size_t ZSTD144_initDStream(ZSTD144_DStream* zds)
{
    DEBUGLOG(4, "ZSTD144_initDStream");
    return ZSTD144_initDStream_usingDDict(zds, NULL);
}

/* ZSTD144_initDStream_usingDDict() :
 * ddict will just be referenced, and must outlive decompression session
 * this function cannot fail */
size_t ZSTD144_initDStream_usingDDict(ZSTD144_DStream* dctx, const ZSTD144_DDict* ddict)
{
    FORWARD_IF_ERROR( ZSTD144_DCtx_reset(dctx, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_DCtx_refDDict(dctx, ddict) );
    return ZSTD144_startingInputLength(dctx->format);
}

/* ZSTD144_resetDStream() :
 * return : expected size, aka ZSTD144_startingInputLength().
 * this function cannot fail */
size_t ZSTD144_resetDStream(ZSTD144_DStream* dctx)
{
    FORWARD_IF_ERROR(ZSTD144_DCtx_reset(dctx, ZSTD144_reset_session_only));
    return ZSTD144_startingInputLength(dctx->format);
}


size_t ZSTD144_DCtx_refDDict(ZSTD144_DCtx* dctx, const ZSTD144_DDict* ddict)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong);
    ZSTD144_clearDict(dctx);
    if (ddict) {
        dctx->ddict = ddict;
        dctx->dictUses = ZSTD144_use_indefinitely;
    }
    return 0;
}

/* ZSTD144_DCtx_setMaxWindowSize() :
 * note : no direct equivalence in ZSTD144_DCtx_setParameter,
 * since this version sets windowSize, and the other sets windowLog */
size_t ZSTD144_DCtx_setMaxWindowSize(ZSTD144_DCtx* dctx, size_t maxWindowSize)
{
    ZSTD144_bounds const bounds = ZSTD144_dParam_getBounds(ZSTD144_d_windowLogMax);
    size_t const min = (size_t)1 << bounds.lowerBound;
    size_t const max = (size_t)1 << bounds.upperBound;
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong);
    RETURN_ERROR_IF(maxWindowSize < min, parameter_outOfBound);
    RETURN_ERROR_IF(maxWindowSize > max, parameter_outOfBound);
    dctx->maxWindowSize = maxWindowSize;
    return 0;
}

size_t ZSTD144_DCtx_setFormat(ZSTD144_DCtx* dctx, ZSTD144_format_e format)
{
    return ZSTD144_DCtx_setParameter(dctx, ZSTD144_d_format, format);
}

ZSTD144_bounds ZSTD144_dParam_getBounds(ZSTD144_dParameter dParam)
{
    ZSTD144_bounds bounds = { 0, 0, 0 };
    switch(dParam) {
        case ZSTD144_d_windowLogMax:
            bounds.lowerBound = ZSTD144_WINDOWLOG_ABSOLUTEMIN;
            bounds.upperBound = ZSTD144_WINDOWLOG_MAX;
            return bounds;
        case ZSTD144_d_format:
            bounds.lowerBound = (int)ZSTD144_f_zstd1;
            bounds.upperBound = (int)ZSTD144_f_zstd1_magicless;
            ZSTD144_STATIC_ASSERT(ZSTD144_f_zstd1 < ZSTD144_f_zstd1_magicless);
            return bounds;
        default:;
    }
    bounds.error = ERROR(parameter_unsupported);
    return bounds;
}

/* ZSTD144_dParam_withinBounds:
 * @return 1 if value is within dParam bounds,
 * 0 otherwise */
static int ZSTD144_dParam_withinBounds(ZSTD144_dParameter dParam, int value)
{
    ZSTD144_bounds const bounds = ZSTD144_dParam_getBounds(dParam);
    if (ZSTD144_isError(bounds.error)) return 0;
    if (value < bounds.lowerBound) return 0;
    if (value > bounds.upperBound) return 0;
    return 1;
}

#define CHECK_DBOUNDS(p,v) {                \
    RETURN_ERROR_IF(!ZSTD144_dParam_withinBounds(p, v), parameter_outOfBound); \
}

size_t ZSTD144_DCtx_setParameter(ZSTD144_DCtx* dctx, ZSTD144_dParameter dParam, int value)
{
    RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong);
    switch(dParam) {
        case ZSTD144_d_windowLogMax:
            if (value == 0) value = ZSTD144_WINDOWLOG_LIMIT_DEFAULT;
            CHECK_DBOUNDS(ZSTD144_d_windowLogMax, value);
            dctx->maxWindowSize = ((size_t)1) << value;
            return 0;
        case ZSTD144_d_format:
            CHECK_DBOUNDS(ZSTD144_d_format, value);
            dctx->format = (ZSTD144_format_e)value;
            return 0;
        default:;
    }
    RETURN_ERROR(parameter_unsupported);
}

size_t ZSTD144_DCtx_reset(ZSTD144_DCtx* dctx, ZSTD144_ResetDirective reset)
{
    if ( (reset == ZSTD144_reset_session_only)
      || (reset == ZSTD144_reset_session_and_parameters) ) {
        dctx->streamStage = zdss_init;
        dctx->noForwardProgress = 0;
    }
    if ( (reset == ZSTD144_reset_parameters)
      || (reset == ZSTD144_reset_session_and_parameters) ) {
        RETURN_ERROR_IF(dctx->streamStage != zdss_init, stage_wrong);
        ZSTD144_clearDict(dctx);
        dctx->format = ZSTD144_f_zstd1;
        dctx->maxWindowSize = ZSTD144_MAXWINDOWSIZE_DEFAULT;
    }
    return 0;
}


size_t ZSTD144_sizeof_DStream(const ZSTD144_DStream* dctx)
{
    return ZSTD144_sizeof_DCtx(dctx);
}

size_t ZSTD144_decodingBufferSize_min(unsigned long long windowSize, unsigned long long frameContentSize)
{
    size_t const blockSize = (size_t) MIN(windowSize, ZSTD144_BLOCKSIZE_MAX);
    unsigned long long const neededRBSize = windowSize + blockSize + (WILDCOPY_OVERLENGTH * 2);
    unsigned long long const neededSize = MIN(frameContentSize, neededRBSize);
    size_t const minRBSize = (size_t) neededSize;
    RETURN_ERROR_IF((unsigned long long)minRBSize != neededSize,
                    frameParameter_windowTooLarge);
    return minRBSize;
}

size_t ZSTD144_estimateDStreamSize(size_t windowSize)
{
    size_t const blockSize = MIN(windowSize, ZSTD144_BLOCKSIZE_MAX);
    size_t const inBuffSize = blockSize;  /* no block can be larger */
    size_t const outBuffSize = ZSTD144_decodingBufferSize_min(windowSize, ZSTD144_CONTENTSIZE_UNKNOWN);
    return ZSTD144_estimateDCtxSize() + inBuffSize + outBuffSize;
}

size_t ZSTD144_estimateDStreamSize_fromFrame(const void* src, size_t srcSize)
{
    U32 const windowSizeMax = 1U << ZSTD144_WINDOWLOG_MAX;   /* note : should be user-selectable, but requires an additional parameter (or a dctx) */
    ZSTD144_frameHeader zfh;
    size_t const err = ZSTD144_getFrameHeader(&zfh, src, srcSize);
    if (ZSTD144_isError(err)) return err;
    RETURN_ERROR_IF(err>0, srcSize_wrong);
    RETURN_ERROR_IF(zfh.windowSize > windowSizeMax,
                    frameParameter_windowTooLarge);
    return ZSTD144_estimateDStreamSize((size_t)zfh.windowSize);
}


/* *****   Decompression   ***** */

MEM_STATIC size_t ZSTD144_limitCopy(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    size_t const length = MIN(dstCapacity, srcSize);
    memcpy(dst, src, length);
    return length;
}


size_t ZSTD144_decompressStream(ZSTD144_DStream* zds, ZSTD144_outBuffer* output, ZSTD144_inBuffer* input)
{
    const char* const istart = (const char*)(input->src) + input->pos;
    const char* const iend = (const char*)(input->src) + input->size;
    const char* ip = istart;
    char* const ostart = (char*)(output->dst) + output->pos;
    char* const oend = (char*)(output->dst) + output->size;
    char* op = ostart;
    U32 someMoreWork = 1;

    DEBUGLOG(5, "ZSTD144_decompressStream");
    RETURN_ERROR_IF(
        input->pos > input->size,
        srcSize_wrong,
        "forbidden. in: pos: %u   vs size: %u",
        (U32)input->pos, (U32)input->size);
    RETURN_ERROR_IF(
        output->pos > output->size,
        dstSize_tooSmall,
        "forbidden. out: pos: %u   vs size: %u",
        (U32)output->pos, (U32)output->size);
    DEBUGLOG(5, "input size : %u", (U32)(input->size - input->pos));

    while (someMoreWork) {
        switch(zds->streamStage)
        {
        case zdss_init :
            DEBUGLOG(5, "stage zdss_init => transparent reset ");
            zds->streamStage = zdss_loadHeader;
            zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
            zds->legacyVersion = 0;
            zds->hostageByte = 0;
            /* fall-through */

        case zdss_loadHeader :
            DEBUGLOG(5, "stage zdss_loadHeader (srcSize : %u)", (U32)(iend - ip));
#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT>=1)
            if (zds->legacyVersion) {
                RETURN_ERROR_IF(zds->staticSize, memory_allocation,
                    "legacy support is incompatible with static dctx");
                {   size_t const hint = ZSTD144_decompressLegacyStream(zds->legacyContext, zds->legacyVersion, output, input);
                    if (hint==0) zds->streamStage = zdss_init;
                    return hint;
            }   }
#endif
            {   size_t const hSize = ZSTD144_getFrameHeader_advanced(&zds->fParams, zds->headerBuffer, zds->lhSize, zds->format);
                DEBUGLOG(5, "header size : %u", (U32)hSize);
                if (ZSTD144_isError(hSize)) {
#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT>=1)
                    U32 const legacyVersion = ZSTD144_isLegacy(istart, iend-istart);
                    if (legacyVersion) {
                        ZSTD144_DDict const* const ddict = ZSTD144_getDDict(zds);
                        const void* const dict = ddict ? ZSTD144_DDict_dictContent(ddict) : NULL;
                        size_t const dictSize = ddict ? ZSTD144_DDict_dictSize(ddict) : 0;
                        DEBUGLOG(5, "ZSTD144_decompressStream: detected legacy version v0.%u", legacyVersion);
                        RETURN_ERROR_IF(zds->staticSize, memory_allocation,
                            "legacy support is incompatible with static dctx");
                        FORWARD_IF_ERROR(ZSTD144_initLegacyStream(&zds->legacyContext,
                                    zds->previousLegacyVersion, legacyVersion,
                                    dict, dictSize));
                        zds->legacyVersion = zds->previousLegacyVersion = legacyVersion;
                        {   size_t const hint = ZSTD144_decompressLegacyStream(zds->legacyContext, legacyVersion, output, input);
                            if (hint==0) zds->streamStage = zdss_init;   /* or stay in stage zdss_loadHeader */
                            return hint;
                    }   }
#endif
                    return hSize;   /* error */
                }
                if (hSize != 0) {   /* need more input */
                    size_t const toLoad = hSize - zds->lhSize;   /* if hSize!=0, hSize > zds->lhSize */
                    size_t const remainingInput = (size_t)(iend-ip);
                    assert(iend >= ip);
                    if (toLoad > remainingInput) {   /* not enough input to load full header */
                        if (remainingInput > 0) {
                            memcpy(zds->headerBuffer + zds->lhSize, ip, remainingInput);
                            zds->lhSize += remainingInput;
                        }
                        input->pos = input->size;
                        return (MAX((size_t)ZSTD144_FRAMEHEADERSIZE_MIN(zds->format), hSize) - zds->lhSize) + ZSTD144_blockHeaderSize;   /* remaining header bytes + next block header */
                    }
                    assert(ip != NULL);
                    memcpy(zds->headerBuffer + zds->lhSize, ip, toLoad); zds->lhSize = hSize; ip += toLoad;
                    break;
            }   }

            /* check for single-pass mode opportunity */
            if (zds->fParams.frameContentSize && zds->fParams.windowSize /* skippable frame if == 0 */
                && (U64)(size_t)(oend-op) >= zds->fParams.frameContentSize) {
                size_t const cSize = ZSTD144_findFrameCompressedSize(istart, iend-istart);
                if (cSize <= (size_t)(iend-istart)) {
                    /* shortcut : using single-pass mode */
                    size_t const decompressedSize = ZSTD144_decompress_usingDDict(zds, op, oend-op, istart, cSize, ZSTD144_getDDict(zds));
                    if (ZSTD144_isError(decompressedSize)) return decompressedSize;
                    DEBUGLOG(4, "shortcut to single-pass ZSTD144_decompress_usingDDict()")
                    ip = istart + cSize;
                    op += decompressedSize;
                    zds->expected = 0;
                    zds->streamStage = zdss_init;
                    someMoreWork = 0;
                    break;
            }   }

            /* Consume header (see ZSTDds_decodeFrameHeader) */
            DEBUGLOG(4, "Consume header");
            FORWARD_IF_ERROR(ZSTD144_decompressBegin_usingDDict(zds, ZSTD144_getDDict(zds)));

            if ((MEM_readLE32(zds->headerBuffer) & ZSTD144_MAGIC_SKIPPABLE_MASK) == ZSTD144_MAGIC_SKIPPABLE_START) {  /* skippable frame */
                zds->expected = MEM_readLE32(zds->headerBuffer + ZSTD144_FRAMEIDSIZE);
                zds->stage = ZSTDds_skipFrame;
            } else {
                FORWARD_IF_ERROR(ZSTD144_decodeFrameHeader(zds, zds->headerBuffer, zds->lhSize));
                zds->expected = ZSTD144_blockHeaderSize;
                zds->stage = ZSTDds_decodeBlockHeader;
            }

            /* control buffer memory usage */
            DEBUGLOG(4, "Control max memory usage (%u KB <= max %u KB)",
                        (U32)(zds->fParams.windowSize >>10),
                        (U32)(zds->maxWindowSize >> 10) );
            zds->fParams.windowSize = MAX(zds->fParams.windowSize, 1U << ZSTD144_WINDOWLOG_ABSOLUTEMIN);
            RETURN_ERROR_IF(zds->fParams.windowSize > zds->maxWindowSize,
                            frameParameter_windowTooLarge);

            /* Adapt buffer sizes to frame header instructions */
            {   size_t const neededInBuffSize = MAX(zds->fParams.blockSizeMax, 4 /* frame checksum */);
                size_t const neededOutBuffSize = ZSTD144_decodingBufferSize_min(zds->fParams.windowSize, zds->fParams.frameContentSize);
                if ((zds->inBuffSize < neededInBuffSize) || (zds->outBuffSize < neededOutBuffSize)) {
                    size_t const bufferSize = neededInBuffSize + neededOutBuffSize;
                    DEBUGLOG(4, "inBuff  : from %u to %u",
                                (U32)zds->inBuffSize, (U32)neededInBuffSize);
                    DEBUGLOG(4, "outBuff : from %u to %u",
                                (U32)zds->outBuffSize, (U32)neededOutBuffSize);
                    if (zds->staticSize) {  /* static DCtx */
                        DEBUGLOG(4, "staticSize : %u", (U32)zds->staticSize);
                        assert(zds->staticSize >= sizeof(ZSTD144_DCtx));  /* controlled at init */
                        RETURN_ERROR_IF(
                            bufferSize > zds->staticSize - sizeof(ZSTD144_DCtx),
                            memory_allocation);
                    } else {
                        ZSTD144_free(zds->inBuff, zds->customMem);
                        zds->inBuffSize = 0;
                        zds->outBuffSize = 0;
                        zds->inBuff = (char*)ZSTD144_malloc(bufferSize, zds->customMem);
                        RETURN_ERROR_IF(zds->inBuff == NULL, memory_allocation);
                    }
                    zds->inBuffSize = neededInBuffSize;
                    zds->outBuff = zds->inBuff + zds->inBuffSize;
                    zds->outBuffSize = neededOutBuffSize;
            }   }
            zds->streamStage = zdss_read;
            /* fall-through */

        case zdss_read:
            DEBUGLOG(5, "stage zdss_read");
            {   size_t const neededInSize = ZSTD144_nextSrcSizeToDecompress(zds);
                DEBUGLOG(5, "neededInSize = %u", (U32)neededInSize);
                if (neededInSize==0) {  /* end of frame */
                    zds->streamStage = zdss_init;
                    someMoreWork = 0;
                    break;
                }
                if ((size_t)(iend-ip) >= neededInSize) {  /* decode directly from src */
                    int const isSkipFrame = ZSTD144_isSkipFrame(zds);
                    size_t const decodedSize = ZSTD144_decompressContinue(zds,
                        zds->outBuff + zds->outStart, (isSkipFrame ? 0 : zds->outBuffSize - zds->outStart),
                        ip, neededInSize);
                    if (ZSTD144_isError(decodedSize)) return decodedSize;
                    ip += neededInSize;
                    if (!decodedSize && !isSkipFrame) break;   /* this was just a header */
                    zds->outEnd = zds->outStart + decodedSize;
                    zds->streamStage = zdss_flush;
                    break;
            }   }
            if (ip==iend) { someMoreWork = 0; break; }   /* no more input */
            zds->streamStage = zdss_load;
            /* fall-through */

        case zdss_load:
            {   size_t const neededInSize = ZSTD144_nextSrcSizeToDecompress(zds);
                size_t const toLoad = neededInSize - zds->inPos;
                int const isSkipFrame = ZSTD144_isSkipFrame(zds);
                size_t loadedSize;
                if (isSkipFrame) {
                    loadedSize = MIN(toLoad, (size_t)(iend-ip));
                } else {
                    RETURN_ERROR_IF(toLoad > zds->inBuffSize - zds->inPos,
                                    corruption_detected,
                                    "should never happen");
                    loadedSize = ZSTD144_limitCopy(zds->inBuff + zds->inPos, toLoad, ip, iend-ip);
                }
                ip += loadedSize;
                zds->inPos += loadedSize;
                if (loadedSize < toLoad) { someMoreWork = 0; break; }   /* not enough input, wait for more */

                /* decode loaded input */
                {   size_t const decodedSize = ZSTD144_decompressContinue(zds,
                        zds->outBuff + zds->outStart, zds->outBuffSize - zds->outStart,
                        zds->inBuff, neededInSize);
                    if (ZSTD144_isError(decodedSize)) return decodedSize;
                    zds->inPos = 0;   /* input is consumed */
                    if (!decodedSize && !isSkipFrame) { zds->streamStage = zdss_read; break; }   /* this was just a header */
                    zds->outEnd = zds->outStart +  decodedSize;
            }   }
            zds->streamStage = zdss_flush;
            /* fall-through */

        case zdss_flush:
            {   size_t const toFlushSize = zds->outEnd - zds->outStart;
                size_t const flushedSize = ZSTD144_limitCopy(op, oend-op, zds->outBuff + zds->outStart, toFlushSize);
                op += flushedSize;
                zds->outStart += flushedSize;
                if (flushedSize == toFlushSize) {  /* flush completed */
                    zds->streamStage = zdss_read;
                    if ( (zds->outBuffSize < zds->fParams.frameContentSize)
                      && (zds->outStart + zds->fParams.blockSizeMax > zds->outBuffSize) ) {
                        DEBUGLOG(5, "restart filling outBuff from beginning (left:%i, needed:%u)",
                                (int)(zds->outBuffSize - zds->outStart),
                                (U32)zds->fParams.blockSizeMax);
                        zds->outStart = zds->outEnd = 0;
                    }
                    break;
            }   }
            /* cannot complete flush */
            someMoreWork = 0;
            break;

        default:
            assert(0);    /* impossible */
            RETURN_ERROR(GENERIC);   /* some compiler require default to do something */
    }   }

    /* result */
    input->pos = (size_t)(ip - (const char*)(input->src));
    output->pos = (size_t)(op - (char*)(output->dst));
    if ((ip==istart) && (op==ostart)) {  /* no forward progress */
        zds->noForwardProgress ++;
        if (zds->noForwardProgress >= ZSTD144_NO_FORWARD_PROGRESS_MAX) {
            RETURN_ERROR_IF(op==oend, dstSize_tooSmall);
            RETURN_ERROR_IF(ip==iend, srcSize_wrong);
            assert(0);
        }
    } else {
        zds->noForwardProgress = 0;
    }
    {   size_t nextSrcSizeHint = ZSTD144_nextSrcSizeToDecompress(zds);
        if (!nextSrcSizeHint) {   /* frame fully decoded */
            if (zds->outEnd == zds->outStart) {  /* output fully flushed */
                if (zds->hostageByte) {
                    if (input->pos >= input->size) {
                        /* can't release hostage (not present) */
                        zds->streamStage = zdss_read;
                        return 1;
                    }
                    input->pos++;  /* release hostage */
                }   /* zds->hostageByte */
                return 0;
            }  /* zds->outEnd == zds->outStart */
            if (!zds->hostageByte) { /* output not fully flushed; keep last byte as hostage; will be released when all output is flushed */
                input->pos--;   /* note : pos > 0, otherwise, impossible to finish reading last block */
                zds->hostageByte=1;
            }
            return 1;
        }  /* nextSrcSizeHint==0 */
        nextSrcSizeHint += ZSTD144_blockHeaderSize * (ZSTD144_nextInputType(zds) == ZSTDnit_block);   /* preload header of next block */
        assert(zds->inPos <= nextSrcSizeHint);
        nextSrcSizeHint -= zds->inPos;   /* part already loaded*/
        return nextSrcSizeHint;
    }
}

size_t ZSTD144_decompressStream_simpleArgs (
                            ZSTD144_DCtx* dctx,
                            void* dst, size_t dstCapacity, size_t* dstPos,
                      const void* src, size_t srcSize, size_t* srcPos)
{
    ZSTD144_outBuffer output = { dst, dstCapacity, *dstPos };
    ZSTD144_inBuffer  input  = { src, srcSize, *srcPos };
    /* ZSTD144_compress_generic() will check validity of dstPos and srcPos */
    size_t const cErr = ZSTD144_decompressStream(dctx, &output, &input);
    *dstPos = output.pos;
    *srcPos = input.pos;
    return cErr;
}
