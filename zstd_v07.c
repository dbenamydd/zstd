/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*- Dependencies -*/
#include <stddef.h>     /* size_t, ptrdiff_t */
#include <string.h>     /* memcpy */
#include <stdlib.h>     /* malloc, free, qsort */

#ifndef XXH_32_STATIC_LINKING_ONLY
#  define XXH_32_STATIC_LINKING_ONLY    /* XXH_3264_state_t */
#endif
#include "xxhash.h"                  /* XXH_3264_* */
#include "zstd_v07.h"

#define FSE144v07_STATIC_LINKING_ONLY   /* FSE144v07_MIN_TABLELOG */
#define HUF144v07_STATIC_LINKING_ONLY   /* HUF144v07_TABLELOG_ABSOLUTEMAX */
#define ZSTD144v07_STATIC_LINKING_ONLY

#include "error_private.h"


#ifdef ZSTD144v07_STATIC_LINKING_ONLY

/* ====================================================================================
 * The definitions in this section are considered experimental.
 * They should never be used with a dynamic library, as they may change in the future.
 * They are provided for advanced usages.
 * Use them only in association with static linking.
 * ==================================================================================== */

/*--- Constants ---*/
#define ZSTD144v07_MAGIC_SKIPPABLE_START  0x184D2A50U

#define ZSTD144v07_WINDOWLOG_MAX_32  25
#define ZSTD144v07_WINDOWLOG_MAX_64  27
#define ZSTD144v07_WINDOWLOG_MAX    ((U32)(MEM_32bits() ? ZSTD144v07_WINDOWLOG_MAX_32 : ZSTD144v07_WINDOWLOG_MAX_64))
#define ZSTD144v07_WINDOWLOG_MIN     18
#define ZSTD144v07_CHAINLOG_MAX     (ZSTD144v07_WINDOWLOG_MAX+1)
#define ZSTD144v07_CHAINLOG_MIN       4
#define ZSTD144v07_HASHLOG_MAX       ZSTD144v07_WINDOWLOG_MAX
#define ZSTD144v07_HASHLOG_MIN       12
#define ZSTD144v07_HASHLOG3_MAX      17
#define ZSTD144v07_SEARCHLOG_MAX    (ZSTD144v07_WINDOWLOG_MAX-1)
#define ZSTD144v07_SEARCHLOG_MIN      1
#define ZSTD144v07_SEARCHLENGTH_MAX   7
#define ZSTD144v07_SEARCHLENGTH_MIN   3
#define ZSTD144v07_TARGETLENGTH_MIN   4
#define ZSTD144v07_TARGETLENGTH_MAX 999

#define ZSTD144v07_FRAMEHEADERSIZE_MAX 18    /* for static allocation */
static const size_t ZSTD144v07_frameHeaderSize_min = 5;
static const size_t ZSTD144v07_frameHeaderSize_max = ZSTD144v07_FRAMEHEADERSIZE_MAX;
static const size_t ZSTD144v07_skippableHeaderSize = 8;  /* magic number + skippable frame length */


/* custom memory allocation functions */
typedef void* (*ZSTD144v07_allocFunction) (void* opaque, size_t size);
typedef void  (*ZSTD144v07_freeFunction) (void* opaque, void* address);
typedef struct { ZSTD144v07_allocFunction customAlloc; ZSTD144v07_freeFunction customFree; void* opaque; } ZSTD144v07_customMem;


/*--- Advanced Decompression functions ---*/

/*! ZSTD144v07_estimateDCtxSize() :
 *  Gives the potential amount of memory allocated to create a ZSTD144v07_DCtx */
ZSTDLIBv07_API size_t ZSTD144v07_estimateDCtxSize(void);

/*! ZSTD144v07_createDCtx_advanced() :
 *  Create a ZSTD decompression context using external alloc and free functions */
ZSTDLIBv07_API ZSTD144v07_DCtx* ZSTD144v07_createDCtx_advanced(ZSTD144v07_customMem customMem);

/*! ZSTD144v07_sizeofDCtx() :
 *  Gives the amount of memory used by a given ZSTD144v07_DCtx */
ZSTDLIBv07_API size_t ZSTD144v07_sizeofDCtx(const ZSTD144v07_DCtx* dctx);


/* ******************************************************************
*  Buffer-less streaming functions (synchronous mode)
********************************************************************/

ZSTDLIBv07_API size_t ZSTD144v07_decompressBegin(ZSTD144v07_DCtx* dctx);
ZSTDLIBv07_API size_t ZSTD144v07_decompressBegin_usingDict(ZSTD144v07_DCtx* dctx, const void* dict, size_t dictSize);
ZSTDLIBv07_API void   ZSTD144v07_copyDCtx(ZSTD144v07_DCtx* dctx, const ZSTD144v07_DCtx* preparedDCtx);

ZSTDLIBv07_API size_t ZSTD144v07_nextSrcSizeToDecompress(ZSTD144v07_DCtx* dctx);
ZSTDLIBv07_API size_t ZSTD144v07_decompressContinue(ZSTD144v07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/*
  Buffer-less streaming decompression (synchronous mode)

  A ZSTD144v07_DCtx object is required to track streaming operations.
  Use ZSTD144v07_createDCtx() / ZSTD144v07_freeDCtx() to manage it.
  A ZSTD144v07_DCtx object can be re-used multiple times.

  First optional operation is to retrieve frame parameters, using ZSTD144v07_getFrameParams(), which doesn't consume the input.
  It can provide the minimum size of rolling buffer required to properly decompress data (`windowSize`),
  and optionally the final size of uncompressed content.
  (Note : content size is an optional info that may not be present. 0 means : content size unknown)
  Frame parameters are extracted from the beginning of compressed frame.
  The amount of data to read is variable, from ZSTD144v07_frameHeaderSize_min to ZSTD144v07_frameHeaderSize_max (so if `srcSize` >= ZSTD144v07_frameHeaderSize_max, it will always work)
  If `srcSize` is too small for operation to succeed, function will return the minimum size it requires to produce a result.
  Result : 0 when successful, it means the ZSTD144v07_frameParams structure has been filled.
          >0 : means there is not enough data into `src`. Provides the expected size to successfully decode header.
           errorCode, which can be tested using ZSTD144v07_isError()

  Start decompression, with ZSTD144v07_decompressBegin() or ZSTD144v07_decompressBegin_usingDict().
  Alternatively, you can copy a prepared context, using ZSTD144v07_copyDCtx().

  Then use ZSTD144v07_nextSrcSizeToDecompress() and ZSTD144v07_decompressContinue() alternatively.
  ZSTD144v07_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD144v07_decompressContinue().
  ZSTD144v07_decompressContinue() requires this exact amount of bytes, or it will fail.

  @result of ZSTD144v07_decompressContinue() is the number of bytes regenerated within 'dst' (necessarily <= dstCapacity).
  It can be zero, which is not an error; it just means ZSTD144v07_decompressContinue() has decoded some header.

  ZSTD144v07_decompressContinue() needs previous data blocks during decompression, up to `windowSize`.
  They should preferably be located contiguously, prior to current block.
  Alternatively, a round buffer of sufficient size is also possible. Sufficient size is determined by frame parameters.
  ZSTD144v07_decompressContinue() is very sensitive to contiguity,
  if 2 blocks don't follow each other, make sure that either the compressor breaks contiguity at the same place,
    or that previous contiguous segment is large enough to properly handle maximum back-reference.

  A frame is fully decoded when ZSTD144v07_nextSrcSizeToDecompress() returns zero.
  Context can then be reset to start a new decompression.


  == Special case : skippable frames ==

  Skippable frames allow the integration of user-defined data into a flow of concatenated frames.
  Skippable frames will be ignored (skipped) by a decompressor. The format of skippable frame is following:
  a) Skippable frame ID - 4 Bytes, Little endian format, any value from 0x184D2A50 to 0x184D2A5F
  b) Frame Size - 4 Bytes, Little endian format, unsigned 32-bits
  c) Frame Content - any content (User Data) of length equal to Frame Size
  For skippable frames ZSTD144v07_decompressContinue() always returns 0.
  For skippable frames ZSTD144v07_getFrameParams() returns fparamsPtr->windowLog==0 what means that a frame is skippable.
  It also returns Frame Size as fparamsPtr->frameContentSize.
*/


/* **************************************
*  Block functions
****************************************/
/*! Block functions produce and decode raw zstd blocks, without frame metadata.
    Frame metadata cost is typically ~18 bytes, which can be non-negligible for very small blocks (< 100 bytes).
    User will have to take in charge required information to regenerate data, such as compressed and content sizes.

    A few rules to respect :
    - Compressing and decompressing require a context structure
      + Use ZSTD144v07_createCCtx() and ZSTD144v07_createDCtx()
    - It is necessary to init context before starting
      + compression : ZSTD144v07_compressBegin()
      + decompression : ZSTD144v07_decompressBegin()
      + variants _usingDict() are also allowed
      + copyCCtx() and copyDCtx() work too
    - Block size is limited, it must be <= ZSTD144v07_getBlockSizeMax()
      + If you need to compress more, cut data into multiple blocks
      + Consider using the regular ZSTD144v07_compress() instead, as frame metadata costs become negligible when source size is large.
    - When a block is considered not compressible enough, ZSTD144v07_compressBlock() result will be zero.
      In which case, nothing is produced into `dst`.
      + User must test for such outcome and deal directly with uncompressed data
      + ZSTD144v07_decompressBlock() doesn't accept uncompressed data as input !!!
      + In case of multiple successive blocks, decoder must be informed of uncompressed block existence to follow proper history.
        Use ZSTD144v07_insertBlock() in such a case.
*/

#define ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX (128 * 1024)   /* define, for static allocation */
ZSTDLIBv07_API size_t ZSTD144v07_decompressBlock(ZSTD144v07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);
ZSTDLIBv07_API size_t ZSTD144v07_insertBlock(ZSTD144v07_DCtx* dctx, const void* blockStart, size_t blockSize);  /**< insert block into `dctx` history. Useful for uncompressed blocks */


#endif   /* ZSTD144v07_STATIC_LINKING_ONLY */


/* ******************************************************************
   mem.h
   low-level memory access routines
   Copyright (C) 2013-2015, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */
#ifndef MEM_H_MODULE
#define MEM_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif

/*-****************************************
*  Compiler specifics
******************************************/
#if defined(_MSC_VER)   /* Visual Studio */
#   include <stdlib.h>  /* _byteswap_ulong */
#   include <intrin.h>  /* _byteswap_* */
#endif
#if defined(__GNUC__)
#  define MEM_STATIC static __attribute__((unused))
#elif defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#  define MEM_STATIC static inline
#elif defined(_MSC_VER)
#  define MEM_STATIC static __inline
#else
#  define MEM_STATIC static  /* this version may generate warnings for unused static functions; disable the relevant warning */
#endif


/*-**************************************************************
*  Basic Types
*****************************************************************/
#if  !defined (__VMS) && (defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */) )
# include <stdint.h>
  typedef  uint8_t BYTE;
  typedef uint16_t U16;
  typedef  int16_t S16;
  typedef uint32_t U32;
  typedef  int32_t S32;
  typedef uint64_t U64;
  typedef  int64_t S64;
#else
  typedef unsigned char       BYTE;
  typedef unsigned short      U16;
  typedef   signed short      S16;
  typedef unsigned int        U32;
  typedef   signed int        S32;
  typedef unsigned long long  U64;
  typedef   signed long long  S64;
#endif


/*-**************************************************************
*  Memory I/O
*****************************************************************/
/* MEM_FORCE_MEMORY_ACCESS :
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method is portable but violate C standard.
 *            It can generate buggy code on targets depending on alignment.
 *            In some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See http://fastcompression.blogspot.fr/2015/08/accessing-unaligned-memory.html for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef MEM_FORCE_MEMORY_ACCESS   /* can be defined externally, on command line for example */
#  if defined(__GNUC__) && ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) )
#    define MEM_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(WIN32)) || \
  (defined(__GNUC__) && ( defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__) ))
#    define MEM_FORCE_MEMORY_ACCESS 1
#  endif
#endif

MEM_STATIC unsigned MEM_32bits(void) { return sizeof(size_t)==4; }
MEM_STATIC unsigned MEM_64bits(void) { return sizeof(size_t)==8; }

MEM_STATIC unsigned MEM_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental  */
    return one.c[0];
}

#if defined(MEM_FORCE_MEMORY_ACCESS) && (MEM_FORCE_MEMORY_ACCESS==2)

/* violates C standard, by lying on structure alignment.
Only use if no other choice to achieve best performance on target platform */
MEM_STATIC U16 MEM_read16(const void* memPtr) { return *(const U16*) memPtr; }
MEM_STATIC U32 MEM_read32(const void* memPtr) { return *(const U32*) memPtr; }
MEM_STATIC U64 MEM_read64(const void* memPtr) { return *(const U64*) memPtr; }

MEM_STATIC void MEM_write16(void* memPtr, U16 value) { *(U16*)memPtr = value; }

#elif defined(MEM_FORCE_MEMORY_ACCESS) && (MEM_FORCE_MEMORY_ACCESS==1)

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; U64 u64; size_t st; } __attribute__((packed)) unalign;

MEM_STATIC U16 MEM_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }
MEM_STATIC U32 MEM_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }
MEM_STATIC U64 MEM_read64(const void* ptr) { return ((const unalign*)ptr)->u64; }

MEM_STATIC void MEM_write16(void* memPtr, U16 value) { ((unalign*)memPtr)->u16 = value; }

#else

/* default method, safe and standard.
   can sometimes prove slower */

MEM_STATIC U16 MEM_read16(const void* memPtr)
{
    U16 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC U32 MEM_read32(const void* memPtr)
{
    U32 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC U64 MEM_read64(const void* memPtr)
{
    U64 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC void MEM_write16(void* memPtr, U16 value)
{
    memcpy(memPtr, &value, sizeof(value));
}

#endif /* MEM_FORCE_MEMORY_ACCESS */

MEM_STATIC U32 MEM_swap32(U32 in)
{
#if defined(_MSC_VER)     /* Visual Studio */
    return _byteswap_ulong(in);
#elif defined (__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
    return __builtin_bswap32(in);
#else
    return  ((in << 24) & 0xff000000 ) |
            ((in <<  8) & 0x00ff0000 ) |
            ((in >>  8) & 0x0000ff00 ) |
            ((in >> 24) & 0x000000ff );
#endif
}

MEM_STATIC U64 MEM_swap64(U64 in)
{
#if defined(_MSC_VER)     /* Visual Studio */
    return _byteswap_uint64(in);
#elif defined (__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
    return __builtin_bswap64(in);
#else
    return  ((in << 56) & 0xff00000000000000ULL) |
            ((in << 40) & 0x00ff000000000000ULL) |
            ((in << 24) & 0x0000ff0000000000ULL) |
            ((in << 8)  & 0x000000ff00000000ULL) |
            ((in >> 8)  & 0x00000000ff000000ULL) |
            ((in >> 24) & 0x0000000000ff0000ULL) |
            ((in >> 40) & 0x000000000000ff00ULL) |
            ((in >> 56) & 0x00000000000000ffULL);
#endif
}


/*=== Little endian r/w ===*/

MEM_STATIC U16 MEM_readLE16(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read16(memPtr);
    else {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)(p[0] + (p[1]<<8));
    }
}

MEM_STATIC void MEM_writeLE16(void* memPtr, U16 val)
{
    if (MEM_isLittleEndian()) {
        MEM_write16(memPtr, val);
    } else {
        BYTE* p = (BYTE*)memPtr;
        p[0] = (BYTE)val;
        p[1] = (BYTE)(val>>8);
    }
}

MEM_STATIC U32 MEM_readLE32(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read32(memPtr);
    else
        return MEM_swap32(MEM_read32(memPtr));
}


MEM_STATIC U64 MEM_readLE64(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read64(memPtr);
    else
        return MEM_swap64(MEM_read64(memPtr));
}

MEM_STATIC size_t MEM_readLEST(const void* memPtr)
{
    if (MEM_32bits())
        return (size_t)MEM_readLE32(memPtr);
    else
        return (size_t)MEM_readLE64(memPtr);
}



#if defined (__cplusplus)
}
#endif

#endif /* MEM_H_MODULE */
/* ******************************************************************
   bitstream
   Part of FSE library
   header file (to include)
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef BITSTREAM_H_MODULE
#define BITSTREAM_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif


/*
*  This API consists of small unitary functions, which must be inlined for best performance.
*  Since link-time-optimization is not available for all compilers,
*  these functions are defined into a .h to be included.
*/


/*=========================================
*  Target specific
=========================================*/
#if defined(__BMI__) && defined(__GNUC__)
#  include <immintrin.h>   /* support for bextr (experimental) */
#endif

/*-********************************************
*  bitStream decoding API (read backward)
**********************************************/
typedef struct
{
    size_t   bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* start;
} BITv07_DStream_t;

typedef enum { BITv07_DStream_unfinished = 0,
               BITv07_DStream_endOfBuffer = 1,
               BITv07_DStream_completed = 2,
               BITv07_DStream_overflow = 3 } BITv07_DStream_status;  /* result of BITv07_reloadDStream() */
               /* 1,2,4,8 would be better for bitmap combinations, but slows down performance a bit ... :( */

MEM_STATIC size_t   BITv07_initDStream(BITv07_DStream_t* bitD, const void* srcBuffer, size_t srcSize);
MEM_STATIC size_t   BITv07_readBits(BITv07_DStream_t* bitD, unsigned nbBits);
MEM_STATIC BITv07_DStream_status BITv07_reloadDStream(BITv07_DStream_t* bitD);
MEM_STATIC unsigned BITv07_endOfDStream(const BITv07_DStream_t* bitD);



/*-****************************************
*  unsafe API
******************************************/
MEM_STATIC size_t BITv07_readBitsFast(BITv07_DStream_t* bitD, unsigned nbBits);
/* faster, but works only if nbBits >= 1 */



/*-**************************************************************
*  Internal functions
****************************************************************/
MEM_STATIC unsigned BITv07_highbit32 (U32 val)
{
#   if defined(_MSC_VER)   /* Visual */
    unsigned long r=0;
    _BitScanReverse ( &r, val );
    return (unsigned) r;
#   elif defined(__GNUC__) && (__GNUC__ >= 3)   /* Use GCC Intrinsic */
    return __builtin_clz (val) ^ 31;
#   else   /* Software version */
    static const unsigned DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return DeBruijnClz[ (U32) (v * 0x07C4ACDDU) >> 27];
#   endif
}



/*-********************************************************
* bitStream decoding
**********************************************************/
/*! BITv07_initDStream() :
*   Initialize a BITv07_DStream_t.
*   `bitD` : a pointer to an already allocated BITv07_DStream_t structure.
*   `srcSize` must be the *exact* size of the bitStream, in bytes.
*   @return : size of stream (== srcSize) or an errorCode if a problem is detected
*/
MEM_STATIC size_t BITv07_initDStream(BITv07_DStream_t* bitD, const void* srcBuffer, size_t srcSize)
{
    if (srcSize < 1) { memset(bitD, 0, sizeof(*bitD)); return ERROR(srcSize_wrong); }

    if (srcSize >=  sizeof(bitD->bitContainer)) {  /* normal case */
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = (const char*)srcBuffer + srcSize - sizeof(bitD->bitContainer);
        bitD->bitContainer = MEM_readLEST(bitD->ptr);
        { BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
          bitD->bitsConsumed = lastByte ? 8 - BITv07_highbit32(lastByte) : 0;
          if (lastByte == 0) return ERROR(GENERIC); /* endMark not present */ }
    } else {
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = bitD->start;
        bitD->bitContainer = *(const BYTE*)(bitD->start);
        switch(srcSize)
        {
            case 7: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[6]) << (sizeof(bitD->bitContainer)*8 - 16);/* fall-through */
            case 6: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[5]) << (sizeof(bitD->bitContainer)*8 - 24);/* fall-through */
            case 5: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[4]) << (sizeof(bitD->bitContainer)*8 - 32);/* fall-through */
            case 4: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[3]) << 24; /* fall-through */
            case 3: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[2]) << 16; /* fall-through */
            case 2: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[1]) <<  8; /* fall-through */
            default: break;
        }
        { BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
          bitD->bitsConsumed = lastByte ? 8 - BITv07_highbit32(lastByte) : 0;
          if (lastByte == 0) return ERROR(GENERIC); /* endMark not present */ }
        bitD->bitsConsumed += (U32)(sizeof(bitD->bitContainer) - srcSize)*8;
    }

    return srcSize;
}


 MEM_STATIC size_t BITv07_lookBits(const BITv07_DStream_t* bitD, U32 nbBits)
{
    U32 const bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return ((bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> 1) >> ((bitMask-nbBits) & bitMask);
}

/*! BITv07_lookBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
MEM_STATIC size_t BITv07_lookBitsFast(const BITv07_DStream_t* bitD, U32 nbBits)
{
    U32 const bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return (bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> (((bitMask+1)-nbBits) & bitMask);
}

MEM_STATIC void BITv07_skipBits(BITv07_DStream_t* bitD, U32 nbBits)
{
    bitD->bitsConsumed += nbBits;
}

MEM_STATIC size_t BITv07_readBits(BITv07_DStream_t* bitD, U32 nbBits)
{
    size_t const value = BITv07_lookBits(bitD, nbBits);
    BITv07_skipBits(bitD, nbBits);
    return value;
}

/*! BITv07_readBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
MEM_STATIC size_t BITv07_readBitsFast(BITv07_DStream_t* bitD, U32 nbBits)
{
    size_t const value = BITv07_lookBitsFast(bitD, nbBits);
    BITv07_skipBits(bitD, nbBits);
    return value;
}

MEM_STATIC BITv07_DStream_status BITv07_reloadDStream(BITv07_DStream_t* bitD)
{
    if (bitD->bitsConsumed > (sizeof(bitD->bitContainer)*8))  /* should not happen => corruption detected */
        return BITv07_DStream_overflow;

    if (bitD->ptr >= bitD->start + sizeof(bitD->bitContainer)) {
        bitD->ptr -= bitD->bitsConsumed >> 3;
        bitD->bitsConsumed &= 7;
        bitD->bitContainer = MEM_readLEST(bitD->ptr);
        return BITv07_DStream_unfinished;
    }
    if (bitD->ptr == bitD->start) {
        if (bitD->bitsConsumed < sizeof(bitD->bitContainer)*8) return BITv07_DStream_endOfBuffer;
        return BITv07_DStream_completed;
    }
    {   U32 nbBytes = bitD->bitsConsumed >> 3;
        BITv07_DStream_status result = BITv07_DStream_unfinished;
        if (bitD->ptr - nbBytes < bitD->start) {
            nbBytes = (U32)(bitD->ptr - bitD->start);  /* ptr > start */
            result = BITv07_DStream_endOfBuffer;
        }
        bitD->ptr -= nbBytes;
        bitD->bitsConsumed -= nbBytes*8;
        bitD->bitContainer = MEM_readLEST(bitD->ptr);   /* reminder : srcSize > sizeof(bitD) */
        return result;
    }
}

/*! BITv07_endOfDStream() :
*   @return Tells if DStream has exactly reached its end (all bits consumed).
*/
MEM_STATIC unsigned BITv07_endOfDStream(const BITv07_DStream_t* DStream)
{
    return ((DStream->ptr == DStream->start) && (DStream->bitsConsumed == sizeof(DStream->bitContainer)*8));
}

#if defined (__cplusplus)
}
#endif

#endif /* BITSTREAM_H_MODULE */
/* ******************************************************************
   FSE : Finite State Entropy codec
   Public Prototypes declaration
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef FSE144v07_H
#define FSE144v07_H

#if defined (__cplusplus)
extern "C" {
#endif



/*-****************************************
*  FSE simple functions
******************************************/

/*! FSE144v07_decompress():
    Decompress FSE data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated destination buffer 'dst', of size 'dstCapacity'.
    @return : size of regenerated data (<= maxDstSize),
              or an error code, which can be tested using FSE144v07_isError() .

    ** Important ** : FSE144v07_decompress() does not decompress non-compressible nor RLE data !!!
    Why ? : making this distinction requires a header.
    Header management is intentionally delegated to the user layer, which can better manage special cases.
*/
size_t FSE144v07_decompress(void* dst,  size_t dstCapacity,
                const void* cSrc, size_t cSrcSize);


/* Error Management */
unsigned    FSE144v07_isError(size_t code);        /* tells if a return value is an error code */
const char* FSE144v07_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/*-*****************************************
*  FSE detailed API
******************************************/
/*!
FSE144v07_decompress() does the following:
1. read normalized counters with readNCount()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using external method.
*/


/* *** DECOMPRESSION *** */

/*! FSE144v07_readNCount():
    Read compactly saved 'normalizedCounter' from 'rBuffer'.
    @return : size read from 'rBuffer',
              or an errorCode, which can be tested using FSE144v07_isError().
              maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
size_t FSE144v07_readNCount (short* normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* rBuffer, size_t rBuffSize);

/*! Constructor and Destructor of FSE144v07_DTable.
    Note that its size depends on 'tableLog' */
typedef unsigned FSE144v07_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */
FSE144v07_DTable* FSE144v07_createDTable(unsigned tableLog);
void        FSE144v07_freeDTable(FSE144v07_DTable* dt);

/*! FSE144v07_buildDTable():
    Builds 'dt', which must be already allocated, using FSE144v07_createDTable().
    return : 0, or an errorCode, which can be tested using FSE144v07_isError() */
size_t FSE144v07_buildDTable (FSE144v07_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! FSE144v07_decompress_usingDTable():
    Decompress compressed source `cSrc` of size `cSrcSize` using `dt`
    into `dst` which must be already allocated.
    @return : size of regenerated data (necessarily <= `dstCapacity`),
              or an errorCode, which can be tested using FSE144v07_isError() */
size_t FSE144v07_decompress_usingDTable(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, const FSE144v07_DTable* dt);

/*!
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by FSE144v07_readNCount() if it was saved using FSE144v07_writeNCount().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of signed short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSE144v07_readNCount() will provide 'tableLog' and 'maxSymbolValue'.
The result of FSE144v07_readNCount() is the number of bytes read from 'rBuffer'.
Note that 'rBufferSize' must be at least 4 bytes, even if useful information is less than that.
If there is an error, the function will return an error code, which can be tested using FSE144v07_isError().

The next step is to build the decompression tables 'FSE144v07_DTable' from 'normalizedCounter'.
This is performed by the function FSE144v07_buildDTable().
The space required by 'FSE144v07_DTable' must be already allocated using FSE144v07_createDTable().
If there is an error, the function will return an error code, which can be tested using FSE144v07_isError().

`FSE144v07_DTable` can then be used to decompress `cSrc`, with FSE144v07_decompress_usingDTable().
`cSrcSize` must be strictly correct, otherwise decompression will fail.
FSE144v07_decompress_usingDTable() result will tell how many bytes were regenerated (<=`dstCapacity`).
If there is an error, the function will return an error code, which can be tested using FSE144v07_isError(). (ex: dst buffer too small)
*/


#ifdef FSE144v07_STATIC_LINKING_ONLY


/* *****************************************
*  Static allocation
*******************************************/
/* FSE buffer bounds */
#define FSE144v07_NCOUNTBOUND 512
#define FSE144v07_BLOCKBOUND(size) (size + (size>>7))

/* It is possible to statically allocate FSE CTable/DTable as a table of unsigned using below macros */
#define FSE144v07_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<maxTableLog))


/* *****************************************
*  FSE advanced API
*******************************************/
size_t FSE144v07_countFast(unsigned* count, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize);
/**< same as FSE144v07_count(), but blindly trusts that all byte values within src are <= *maxSymbolValuePtr  */

unsigned FSE144v07_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
/**< same as FSE144v07_optimalTableLog(), which used `minus==2` */

size_t FSE144v07_buildDTable_raw (FSE144v07_DTable* dt, unsigned nbBits);
/**< build a fake FSE144v07_DTable, designed to read an uncompressed bitstream where each symbol uses nbBits */

size_t FSE144v07_buildDTable_rle (FSE144v07_DTable* dt, unsigned char symbolValue);
/**< build a fake FSE144v07_DTable, designed to always generate the same symbolValue */



/* *****************************************
*  FSE symbol decompression API
*******************************************/
typedef struct
{
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} FSE144v07_DState_t;


static void     FSE144v07_initDState(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD, const FSE144v07_DTable* dt);

static unsigned char FSE144v07_decodeSymbol(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD);



/* *****************************************
*  FSE unsafe API
*******************************************/
static unsigned char FSE144v07_decodeSymbolFast(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD);
/* faster, but works only if nbBits is always >= 1 (otherwise, result will be corrupted) */


/* ======    Decompression    ====== */

typedef struct {
    U16 tableLog;
    U16 fastMode;
} FSE144v07_DTableHeader;   /* sizeof U32 */

typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} FSE144v07_decode_t;   /* size == U32 */

MEM_STATIC void FSE144v07_initDState(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD, const FSE144v07_DTable* dt)
{
    const void* ptr = dt;
    const FSE144v07_DTableHeader* const DTableH = (const FSE144v07_DTableHeader*)ptr;
    DStatePtr->state = BITv07_readBits(bitD, DTableH->tableLog);
    BITv07_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

MEM_STATIC BYTE FSE144v07_peekSymbol(const FSE144v07_DState_t* DStatePtr)
{
    FSE144v07_decode_t const DInfo = ((const FSE144v07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    return DInfo.symbol;
}

MEM_STATIC void FSE144v07_updateState(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSE144v07_decode_t const DInfo = ((const FSE144v07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BITv07_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;
}

MEM_STATIC BYTE FSE144v07_decodeSymbol(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSE144v07_decode_t const DInfo = ((const FSE144v07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BITv07_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/*! FSE144v07_decodeSymbolFast() :
    unsafe, only works if no symbol has a probability > 50% */
MEM_STATIC BYTE FSE144v07_decodeSymbolFast(FSE144v07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSE144v07_decode_t const DInfo = ((const FSE144v07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BITv07_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}



#ifndef FSE144v07_COMMONDEFS_ONLY

/* **************************************************************
*  Tuning parameters
****************************************************************/
/*!MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#define FSE144v07_MAX_MEMORY_USAGE 14
#define FSE144v07_DEFAULT_MEMORY_USAGE 13

/*!FSE144v07_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#define FSE144v07_MAX_SYMBOL_VALUE 255


/* **************************************************************
*  template functions type & suffix
****************************************************************/
#define FSE144v07_FUNCTION_TYPE BYTE
#define FSE144v07_FUNCTION_EXTENSION
#define FSE144v07_DECODE_TYPE FSE144v07_decode_t


#endif   /* !FSE144v07_COMMONDEFS_ONLY */


/* ***************************************************************
*  Constants
*****************************************************************/
#define FSE144v07_MAX_TABLELOG  (FSE144v07_MAX_MEMORY_USAGE-2)
#define FSE144v07_MAX_TABLESIZE (1U<<FSE144v07_MAX_TABLELOG)
#define FSE144v07_MAXTABLESIZE_MASK (FSE144v07_MAX_TABLESIZE-1)
#define FSE144v07_DEFAULT_TABLELOG (FSE144v07_DEFAULT_MEMORY_USAGE-2)
#define FSE144v07_MIN_TABLELOG 5

#define FSE144v07_TABLELOG_ABSOLUTE_MAX 15
#if FSE144v07_MAX_TABLELOG > FSE144v07_TABLELOG_ABSOLUTE_MAX
#  error "FSE144v07_MAX_TABLELOG > FSE144v07_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define FSE144v07_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)


#endif /* FSE144v07_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif

#endif  /* FSE144v07_H */
/* ******************************************************************
   Huffman coder, part of New Generation Entropy library
   header file
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef HUF144v07_H_298734234
#define HUF144v07_H_298734234

#if defined (__cplusplus)
extern "C" {
#endif



/* *** simple functions *** */
/**
HUF144v07_decompress() :
    Decompress HUF data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated buffer 'dst', of minimum size 'dstSize'.
    `dstSize` : **must** be the ***exact*** size of original (uncompressed) data.
    Note : in contrast with FSE, HUF144v07_decompress can regenerate
           RLE (cSrcSize==1) and uncompressed (cSrcSize==dstSize) data,
           because it knows size to regenerate.
    @return : size of regenerated data (== dstSize),
              or an error code, which can be tested using HUF144v07_isError()
*/
size_t HUF144v07_decompress(void* dst,  size_t dstSize,
                const void* cSrc, size_t cSrcSize);


/* ****************************************
*  Tool functions
******************************************/
#define HUF144v07_BLOCKSIZE_MAX (128 * 1024)

/* Error Management */
unsigned    HUF144v07_isError(size_t code);        /**< tells if a return value is an error code */
const char* HUF144v07_getErrorName(size_t code);   /**< provides error code string (useful for debugging) */


/* *** Advanced function *** */


#ifdef HUF144v07_STATIC_LINKING_ONLY


/* *** Constants *** */
#define HUF144v07_TABLELOG_ABSOLUTEMAX  16   /* absolute limit of HUF144v07_MAX_TABLELOG. Beyond that value, code does not work */
#define HUF144v07_TABLELOG_MAX  12           /* max configured tableLog (for static allocation); can be modified up to HUF144v07_ABSOLUTEMAX_TABLELOG */
#define HUF144v07_TABLELOG_DEFAULT  11       /* tableLog by default, when not specified */
#define HUF144v07_SYMBOLVALUE_MAX 255
#if (HUF144v07_TABLELOG_MAX > HUF144v07_TABLELOG_ABSOLUTEMAX)
#  error "HUF144v07_TABLELOG_MAX is too large !"
#endif


/* ****************************************
*  Static allocation
******************************************/
/* HUF buffer bounds */
#define HUF144v07_BLOCKBOUND(size) (size + (size>>8) + 8)   /* only true if incompressible pre-filtered with fast heuristic */

/* static allocation of HUF's DTable */
typedef U32 HUF144v07_DTable;
#define HUF144v07_DTABLE_SIZE(maxTableLog)   (1 + (1<<(maxTableLog)))
#define HUF144v07_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) \
        HUF144v07_DTable DTable[HUF144v07_DTABLE_SIZE((maxTableLog)-1)] = { ((U32)((maxTableLog)-1)*0x1000001) }
#define HUF144v07_CREATE_STATIC_DTABLEX4(DTable, maxTableLog) \
        HUF144v07_DTable DTable[HUF144v07_DTABLE_SIZE(maxTableLog)] = { ((U32)(maxTableLog)*0x1000001) }


/* ****************************************
*  Advanced decompression functions
******************************************/
size_t HUF144v07_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUF144v07_decompress4X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */

size_t HUF144v07_decompress4X_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< decodes RLE and uncompressed */
size_t HUF144v07_decompress4X_hufOnly(HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize); /**< considers RLE and uncompressed as errors */
size_t HUF144v07_decompress4X2_DCtx(HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUF144v07_decompress4X4_DCtx(HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */

size_t HUF144v07_decompress1X_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);
size_t HUF144v07_decompress1X2_DCtx(HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUF144v07_decompress1X4_DCtx(HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */


/* ****************************************
*  HUF detailed API
******************************************/
/*!
The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and regenerate 'CTable' using external methods.
*/
/* FSE144v07_count() : find it within "fse.h" */

/*! HUF144v07_readStats() :
    Read compact Huffman tree, saved by HUF144v07_writeCTable().
    `huffWeight` is destination buffer.
    @return : size read from `src` , or an error Code .
    Note : Needed by HUF144v07_readCTable() and HUF144v07_readDTableXn() . */
size_t HUF144v07_readStats(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize);


/*
HUF144v07_decompress() does the following:
1. select the decompression algorithm (X2, X4) based on pre-computed heuristics
2. build Huffman table from save, using HUF144v07_readDTableXn()
3. decode 1 or 4 segments in parallel using HUF144v07_decompressSXn_usingDTable
*/

/** HUF144v07_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUF144v07_decompress4X2, 1==HUF144v07_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUF144v07_selectDecoder (size_t dstSize, size_t cSrcSize);

size_t HUF144v07_readDTableX2 (HUF144v07_DTable* DTable, const void* src, size_t srcSize);
size_t HUF144v07_readDTableX4 (HUF144v07_DTable* DTable, const void* src, size_t srcSize);

size_t HUF144v07_decompress4X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);
size_t HUF144v07_decompress4X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);
size_t HUF144v07_decompress4X4_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);


/* single stream variants */
size_t HUF144v07_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* single-symbol decoder */
size_t HUF144v07_decompress1X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* double-symbol decoder */

size_t HUF144v07_decompress1X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);
size_t HUF144v07_decompress1X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);
size_t HUF144v07_decompress1X4_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUF144v07_DTable* DTable);


#endif /* HUF144v07_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif

#endif   /* HUF144v07_H_298734234 */
/*
   Common functions of New Generation Entropy library
   Copyright (C) 2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
*************************************************************************** */



/*-****************************************
*  FSE Error Management
******************************************/
unsigned FSE144v07_isError(size_t code) { return ERR144_isError(code); }

const char* FSE144v07_getErrorName(size_t code) { return ERR144_getErrorName(code); }


/* **************************************************************
*  HUF Error Management
****************************************************************/
unsigned HUF144v07_isError(size_t code) { return ERR144_isError(code); }

const char* HUF144v07_getErrorName(size_t code) { return ERR144_getErrorName(code); }


/*-**************************************************************
*  FSE NCount encoding-decoding
****************************************************************/
static short FSE144v07_abs(short a) { return (short)(a<0 ? -a : a); }

size_t FSE144v07_readNCount (short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                 const void* headerBuffer, size_t hbSize)
{
    const BYTE* const istart = (const BYTE*) headerBuffer;
    const BYTE* const iend = istart + hbSize;
    const BYTE* ip = istart;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    unsigned charnum = 0;
    int previous0 = 0;

    if (hbSize < 4) return ERROR(srcSize_wrong);
    bitStream = MEM_readLE32(ip);
    nbBits = (bitStream & 0xF) + FSE144v07_MIN_TABLELOG;   /* extract tableLog */
    if (nbBits > FSE144v07_TABLELOG_ABSOLUTE_MAX) return ERROR(tableLog_tooLarge);
    bitStream >>= 4;
    bitCount = 4;
    *tableLogPtr = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;

    while ((remaining>1) && (charnum<=*maxSVPtr)) {
        if (previous0) {
            unsigned n0 = charnum;
            while ((bitStream & 0xFFFF) == 0xFFFF) {
                n0+=24;
                if (ip < iend-5) {
                    ip+=2;
                    bitStream = MEM_readLE32(ip) >> bitCount;
                } else {
                    bitStream >>= 16;
                    bitCount+=16;
            }   }
            while ((bitStream & 3) == 3) {
                n0+=3;
                bitStream>>=2;
                bitCount+=2;
            }
            n0 += bitStream & 3;
            bitCount += 2;
            if (n0 > *maxSVPtr) return ERROR(maxSymbolValue_tooSmall);
            while (charnum < n0) normalizedCounter[charnum++] = 0;
            if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                ip += bitCount>>3;
                bitCount &= 7;
                bitStream = MEM_readLE32(ip) >> bitCount;
            }
            else
                bitStream >>= 2;
        }
        {   short const max = (short)((2*threshold-1)-remaining);
            short count;

            if ((bitStream & (threshold-1)) < (U32)max) {
                count = (short)(bitStream & (threshold-1));
                bitCount   += nbBits-1;
            } else {
                count = (short)(bitStream & (2*threshold-1));
                if (count >= threshold) count -= max;
                bitCount   += nbBits;
            }

            count--;   /* extra accuracy */
            remaining -= FSE144v07_abs(count);
            normalizedCounter[charnum++] = count;
            previous0 = !count;
            while (remaining < threshold) {
                nbBits--;
                threshold >>= 1;
            }

            if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                ip += bitCount>>3;
                bitCount &= 7;
            } else {
                bitCount -= (int)(8 * (iend - 4 - ip));
                ip = iend - 4;
            }
            bitStream = MEM_readLE32(ip) >> (bitCount & 31);
    }   }   /* while ((remaining>1) && (charnum<=*maxSVPtr)) */
    if (remaining != 1) return ERROR(GENERIC);
    *maxSVPtr = charnum-1;

    ip += (bitCount+7)>>3;
    if ((size_t)(ip-istart) > hbSize) return ERROR(srcSize_wrong);
    return ip-istart;
}


/*! HUF144v07_readStats() :
    Read compact Huffman tree, saved by HUF144v07_writeCTable().
    `huffWeight` is destination buffer.
    @return : size read from `src` , or an error Code .
    Note : Needed by HUF144v07_readCTable() and HUF144v07_readDTableXn() .
*/
size_t HUF144v07_readStats(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize)
{
    U32 weightTotal;
    const BYTE* ip = (const BYTE*) src;
    size_t iSize;
    size_t oSize;

    if (!srcSize) return ERROR(srcSize_wrong);
    iSize = ip[0];
    //memset(huffWeight, 0, hwSize);   /* is not necessary, even though some analyzer complain ... */

    if (iSize >= 128)  { /* special header */
        if (iSize >= (242)) {  /* RLE */
            static U32 l[14] = { 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128 };
            oSize = l[iSize-242];
            memset(huffWeight, 1, hwSize);
            iSize = 0;
        }
        else {   /* Incompressible */
            oSize = iSize - 127;
            iSize = ((oSize+1)/2);
            if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
            if (oSize >= hwSize) return ERROR(corruption_detected);
            ip += 1;
            {   U32 n;
                for (n=0; n<oSize; n+=2) {
                    huffWeight[n]   = ip[n/2] >> 4;
                    huffWeight[n+1] = ip[n/2] & 15;
    }   }   }   }
    else  {   /* header compressed with FSE (normal case) */
        if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
        oSize = FSE144v07_decompress(huffWeight, hwSize-1, ip+1, iSize);   /* max (hwSize-1) values decoded, as last one is implied */
        if (FSE144v07_isError(oSize)) return oSize;
    }

    /* collect weight stats */
    memset(rankStats, 0, (HUF144v07_TABLELOG_ABSOLUTEMAX + 1) * sizeof(U32));
    weightTotal = 0;
    {   U32 n; for (n=0; n<oSize; n++) {
            if (huffWeight[n] >= HUF144v07_TABLELOG_ABSOLUTEMAX) return ERROR(corruption_detected);
            rankStats[huffWeight[n]]++;
            weightTotal += (1 << huffWeight[n]) >> 1;
    }   }
    if (weightTotal == 0) return ERROR(corruption_detected);

    /* get last non-null symbol weight (implied, total must be 2^n) */
    {   U32 const tableLog = BITv07_highbit32(weightTotal) + 1;
        if (tableLog > HUF144v07_TABLELOG_ABSOLUTEMAX) return ERROR(corruption_detected);
        *tableLogPtr = tableLog;
        /* determine last weight */
        {   U32 const total = 1 << tableLog;
            U32 const rest = total - weightTotal;
            U32 const verif = 1 << BITv07_highbit32(rest);
            U32 const lastWeight = BITv07_highbit32(rest) + 1;
            if (verif != rest) return ERROR(corruption_detected);    /* last value must be a clean power of 2 */
            huffWeight[oSize] = (BYTE)lastWeight;
            rankStats[lastWeight]++;
    }   }

    /* check tree construction validity */
    if ((rankStats[1] < 2) || (rankStats[1] & 1)) return ERROR(corruption_detected);   /* by construction : at least 2 elts of rank 1, must be even */

    /* results */
    *nbSymbolsPtr = (U32)(oSize+1);
    return iSize+1;
}
/* ******************************************************************
   FSE : Finite State Entropy decoder
   Copyright (C) 2013-2015, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */


/* **************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#else
#  if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#    ifdef __GNUC__
#      define FORCE_INLINE static inline __attribute__((always_inline))
#    else
#      define FORCE_INLINE static inline
#    endif
#  else
#    define FORCE_INLINE static
#  endif /* __STDC_VERSION__ */
#endif


/* **************************************************************
*  Error Management
****************************************************************/
#define FSE144v07_isError ERR144_isError
#define FSE144v07_STATIC_ASSERT(c) { enum { FSE144v07_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/* **************************************************************
*  Complex types
****************************************************************/
typedef U32 DTable_max_t[FSE144v07_DTABLE_SIZE_U32(FSE144v07_MAX_TABLELOG)];


/* **************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef FSE144v07_FUNCTION_EXTENSION
#  error "FSE144v07_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE144v07_FUNCTION_TYPE
#  error "FSE144v07_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define FSE144v07_CAT(X,Y) X##Y
#define FSE144v07_FUNCTION_NAME(X,Y) FSE144v07_CAT(X,Y)
#define FSE144v07_TYPE_NAME(X,Y) FSE144v07_CAT(X,Y)


/* Function templates */
FSE144v07_DTable* FSE144v07_createDTable (unsigned tableLog)
{
    if (tableLog > FSE144v07_TABLELOG_ABSOLUTE_MAX) tableLog = FSE144v07_TABLELOG_ABSOLUTE_MAX;
    return (FSE144v07_DTable*)malloc( FSE144v07_DTABLE_SIZE_U32(tableLog) * sizeof (U32) );
}

void FSE144v07_freeDTable (FSE144v07_DTable* dt)
{
    free(dt);
}

size_t FSE144v07_buildDTable(FSE144v07_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    void* const tdPtr = dt+1;   /* because *dt is unsigned, 32-bits aligned on 32-bits */
    FSE144v07_DECODE_TYPE* const tableDecode = (FSE144v07_DECODE_TYPE*) (tdPtr);
    U16 symbolNext[FSE144v07_MAX_SYMBOL_VALUE+1];

    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;
    U32 highThreshold = tableSize-1;

    /* Sanity Checks */
    if (maxSymbolValue > FSE144v07_MAX_SYMBOL_VALUE) return ERROR(maxSymbolValue_tooLarge);
    if (tableLog > FSE144v07_MAX_TABLELOG) return ERROR(tableLog_tooLarge);

    /* Init, lay down lowprob symbols */
    {   FSE144v07_DTableHeader DTableH;
        DTableH.tableLog = (U16)tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].symbol = (FSE144v07_FUNCTION_TYPE)s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    symbolNext[s] = normalizedCounter[s];
        }   }   }
        memcpy(dt, &DTableH, sizeof(DTableH));
    }

    /* Spread symbols */
    {   U32 const tableMask = tableSize-1;
        U32 const step = FSE144v07_TABLESTEP(tableSize);
        U32 s, position = 0;
        for (s=0; s<maxSV1; s++) {
            int i;
            for (i=0; i<normalizedCounter[s]; i++) {
                tableDecode[position].symbol = (FSE144v07_FUNCTION_TYPE)s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }

        if (position!=0) return ERROR(GENERIC);   /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {   U32 u;
        for (u=0; u<tableSize; u++) {
            FSE144v07_FUNCTION_TYPE const symbol = (FSE144v07_FUNCTION_TYPE)(tableDecode[u].symbol);
            U16 nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - BITv07_highbit32 ((U32)nextState) );
            tableDecode[u].newState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
    }   }

    return 0;
}



#ifndef FSE144v07_COMMONDEFS_ONLY

/*-*******************************************************
*  Decompression (Byte symbols)
*********************************************************/
size_t FSE144v07_buildDTable_rle (FSE144v07_DTable* dt, BYTE symbolValue)
{
    void* ptr = dt;
    FSE144v07_DTableHeader* const DTableH = (FSE144v07_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSE144v07_decode_t* const cell = (FSE144v07_decode_t*)dPtr;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->newState = 0;
    cell->symbol = symbolValue;
    cell->nbBits = 0;

    return 0;
}


size_t FSE144v07_buildDTable_raw (FSE144v07_DTable* dt, unsigned nbBits)
{
    void* ptr = dt;
    FSE144v07_DTableHeader* const DTableH = (FSE144v07_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSE144v07_decode_t* const dinfo = (FSE144v07_decode_t*)dPtr;
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSV1 = tableMask+1;
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return ERROR(GENERIC);         /* min size */

    /* Build Decoding Table */
    DTableH->tableLog = (U16)nbBits;
    DTableH->fastMode = 1;
    for (s=0; s<maxSV1; s++) {
        dinfo[s].newState = 0;
        dinfo[s].symbol = (BYTE)s;
        dinfo[s].nbBits = (BYTE)nbBits;
    }

    return 0;
}

FORCE_INLINE size_t FSE144v07_decompress_usingDTable_generic(
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const FSE144v07_DTable* dt, const unsigned fast)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-3;

    BITv07_DStream_t bitD;
    FSE144v07_DState_t state1;
    FSE144v07_DState_t state2;

    /* Init */
    { size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);   /* replaced last arg by maxCompressed Size */
      if (FSE144v07_isError(errorCode)) return errorCode; }

    FSE144v07_initDState(&state1, &bitD, dt);
    FSE144v07_initDState(&state2, &bitD, dt);

#define FSE144v07_GETSYMBOL(statePtr) fast ? FSE144v07_decodeSymbolFast(statePtr, &bitD) : FSE144v07_decodeSymbol(statePtr, &bitD)

    /* 4 symbols per loop */
    for ( ; (BITv07_reloadDStream(&bitD)==BITv07_DStream_unfinished) && (op<olimit) ; op+=4) {
        op[0] = FSE144v07_GETSYMBOL(&state1);

        if (FSE144v07_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BITv07_reloadDStream(&bitD);

        op[1] = FSE144v07_GETSYMBOL(&state2);

        if (FSE144v07_MAX_TABLELOG*4+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            { if (BITv07_reloadDStream(&bitD) > BITv07_DStream_unfinished) { op+=2; break; } }

        op[2] = FSE144v07_GETSYMBOL(&state1);

        if (FSE144v07_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BITv07_reloadDStream(&bitD);

        op[3] = FSE144v07_GETSYMBOL(&state2);
    }

    /* tail */
    /* note : BITv07_reloadDStream(&bitD) >= FSE144v07_DStream_partiallyFilled; Ends at exactly BITv07_DStream_completed */
    while (1) {
        if (op>(omax-2)) return ERROR(dstSize_tooSmall);

        *op++ = FSE144v07_GETSYMBOL(&state1);

        if (BITv07_reloadDStream(&bitD)==BITv07_DStream_overflow) {
            *op++ = FSE144v07_GETSYMBOL(&state2);
            break;
        }

        if (op>(omax-2)) return ERROR(dstSize_tooSmall);

        *op++ = FSE144v07_GETSYMBOL(&state2);

        if (BITv07_reloadDStream(&bitD)==BITv07_DStream_overflow) {
            *op++ = FSE144v07_GETSYMBOL(&state1);
            break;
    }   }

    return op-ostart;
}


size_t FSE144v07_decompress_usingDTable(void* dst, size_t originalSize,
                            const void* cSrc, size_t cSrcSize,
                            const FSE144v07_DTable* dt)
{
    const void* ptr = dt;
    const FSE144v07_DTableHeader* DTableH = (const FSE144v07_DTableHeader*)ptr;
    const U32 fastMode = DTableH->fastMode;

    /* select fast mode (static) */
    if (fastMode) return FSE144v07_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
    return FSE144v07_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}


size_t FSE144v07_decompress(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* const istart = (const BYTE*)cSrc;
    const BYTE* ip = istart;
    short counting[FSE144v07_MAX_SYMBOL_VALUE+1];
    DTable_max_t dt;   /* Static analyzer seems unable to understand this table will be properly initialized later */
    unsigned tableLog;
    unsigned maxSymbolValue = FSE144v07_MAX_SYMBOL_VALUE;

    if (cSrcSize<2) return ERROR(srcSize_wrong);   /* too small input size */

    /* normal FSE decoding mode */
    {   size_t const NCountLength = FSE144v07_readNCount (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
        if (FSE144v07_isError(NCountLength)) return NCountLength;
        if (NCountLength >= cSrcSize) return ERROR(srcSize_wrong);   /* too small input size */
        ip += NCountLength;
        cSrcSize -= NCountLength;
    }

    { size_t const errorCode = FSE144v07_buildDTable (dt, counting, maxSymbolValue, tableLog);
      if (FSE144v07_isError(errorCode)) return errorCode; }

    return FSE144v07_decompress_usingDTable (dst, maxDstSize, ip, cSrcSize, dt);   /* always return, even if it is an error code */
}



#endif   /* FSE144v07_COMMONDEFS_ONLY */

/* ******************************************************************
   Huffman decoder, part of New Generation Entropy library
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/* **************************************************************
*  Compiler specifics
****************************************************************/
#if defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
/* inline is defined */
#elif defined(_MSC_VER)
#  define inline __inline
#else
#  define inline /* disable inline */
#endif


#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#endif



/* **************************************************************
*  Error Management
****************************************************************/
#define HUF144v07_STATIC_ASSERT(c) { enum { HUF144v07_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/*-***************************/
/*  generic DTableDesc       */
/*-***************************/

typedef struct { BYTE maxTableLog; BYTE tableType; BYTE tableLog; BYTE reserved; } DTableDesc;

static DTableDesc HUF144v07_getDTableDesc(const HUF144v07_DTable* table)
{
    DTableDesc dtd;
    memcpy(&dtd, table, sizeof(dtd));
    return dtd;
}


/*-***************************/
/*  single-symbol decoding   */
/*-***************************/

typedef struct { BYTE byte; BYTE nbBits; } HUF144v07_DEltX2;   /* single-symbol decoding */

size_t HUF144v07_readDTableX2 (HUF144v07_DTable* DTable, const void* src, size_t srcSize)
{
    BYTE huffWeight[HUF144v07_SYMBOLVALUE_MAX + 1];
    U32 rankVal[HUF144v07_TABLELOG_ABSOLUTEMAX + 1];   /* large enough for values from 0 to 16 */
    U32 tableLog = 0;
    U32 nbSymbols = 0;
    size_t iSize;
    void* const dtPtr = DTable + 1;
    HUF144v07_DEltX2* const dt = (HUF144v07_DEltX2*)dtPtr;

    HUF144v07_STATIC_ASSERT(sizeof(DTableDesc) == sizeof(HUF144v07_DTable));
    //memset(huffWeight, 0, sizeof(huffWeight));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUF144v07_readStats(huffWeight, HUF144v07_SYMBOLVALUE_MAX + 1, rankVal, &nbSymbols, &tableLog, src, srcSize);
    if (HUF144v07_isError(iSize)) return iSize;

    /* Table header */
    {   DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
        if (tableLog > (U32)(dtd.maxTableLog+1)) return ERROR(tableLog_tooLarge);   /* DTable too small, huffman tree cannot fit in */
        dtd.tableType = 0;
        dtd.tableLog = (BYTE)tableLog;
        memcpy(DTable, &dtd, sizeof(dtd));
    }

    /* Prepare ranks */
    {   U32 n, nextRankStart = 0;
        for (n=1; n<tableLog+1; n++) {
            U32 current = nextRankStart;
            nextRankStart += (rankVal[n] << (n-1));
            rankVal[n] = current;
    }   }

    /* fill DTable */
    {   U32 n;
        for (n=0; n<nbSymbols; n++) {
            U32 const w = huffWeight[n];
            U32 const length = (1 << w) >> 1;
            U32 i;
            HUF144v07_DEltX2 D;
            D.byte = (BYTE)n; D.nbBits = (BYTE)(tableLog + 1 - w);
            for (i = rankVal[w]; i < rankVal[w] + length; i++)
                dt[i] = D;
            rankVal[w] += length;
    }   }

    return iSize;
}


static BYTE HUF144v07_decodeSymbolX2(BITv07_DStream_t* Dstream, const HUF144v07_DEltX2* dt, const U32 dtLog)
{
    size_t const val = BITv07_lookBitsFast(Dstream, dtLog); /* note : dtLog >= 1 */
    BYTE const c = dt[val].byte;
    BITv07_skipBits(Dstream, dt[val].nbBits);
    return c;
}

#define HUF144v07_DECODE_SYMBOLX2_0(ptr, DStreamPtr) \
    *ptr++ = HUF144v07_decodeSymbolX2(DStreamPtr, dt, dtLog)

#define HUF144v07_DECODE_SYMBOLX2_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUF144v07_TABLELOG_MAX<=12)) \
        HUF144v07_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

#define HUF144v07_DECODE_SYMBOLX2_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        HUF144v07_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

static inline size_t HUF144v07_decodeStreamX2(BYTE* p, BITv07_DStream_t* const bitDPtr, BYTE* const pEnd, const HUF144v07_DEltX2* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 4 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p <= pEnd-4)) {
        HUF144v07_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX2_1(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX2_0(p, bitDPtr);
    }

    /* closer to the end */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p < pEnd))
        HUF144v07_DECODE_SYMBOLX2_0(p, bitDPtr);

    /* no more data to retrieve from bitstream, hence no need to reload */
    while (p < pEnd)
        HUF144v07_DECODE_SYMBOLX2_0(p, bitDPtr);

    return pEnd-pStart;
}

static size_t HUF144v07_decompress1X2_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + dstSize;
    const void* dtPtr = DTable + 1;
    const HUF144v07_DEltX2* const dt = (const HUF144v07_DEltX2*)dtPtr;
    BITv07_DStream_t bitD;
    DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
    U32 const dtLog = dtd.tableLog;

    { size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);
      if (HUF144v07_isError(errorCode)) return errorCode; }

    HUF144v07_decodeStreamX2(op, &bitD, oend, dt, dtLog);

    /* check */
    if (!BITv07_endOfDStream(&bitD)) return ERROR(corruption_detected);

    return dstSize;
}

size_t HUF144v07_decompress1X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
    if (dtd.tableType != 0) return ERROR(GENERIC);
    return HUF144v07_decompress1X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF144v07_decompress1X2_DCtx (HUF144v07_DTable* DCtx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF144v07_readDTableX2 (DCtx, cSrc, cSrcSize);
    if (HUF144v07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUF144v07_decompress1X2_usingDTable_internal (dst, dstSize, ip, cSrcSize, DCtx);
}

size_t HUF144v07_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF144v07_CREATE_STATIC_DTABLEX2(DTable, HUF144v07_TABLELOG_MAX);
    return HUF144v07_decompress1X2_DCtx (DTable, dst, dstSize, cSrc, cSrcSize);
}


static size_t HUF144v07_decompress4X2_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    /* Check */
    if (cSrcSize < 10) return ERROR(corruption_detected);  /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable + 1;
        const HUF144v07_DEltX2* const dt = (const HUF144v07_DEltX2*)dtPtr;

        /* Init */
        BITv07_DStream_t bitD1;
        BITv07_DStream_t bitD2;
        BITv07_DStream_t bitD3;
        BITv07_DStream_t bitD4;
        size_t const length1 = MEM_readLE16(istart);
        size_t const length2 = MEM_readLE16(istart+2);
        size_t const length3 = MEM_readLE16(istart+4);
        size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;
        DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
        U32 const dtLog = dtd.tableLog;

        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        { size_t const errorCode = BITv07_initDStream(&bitD1, istart1, length1);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD2, istart2, length2);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD3, istart3, length3);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD4, istart4, length4);
          if (HUF144v07_isError(errorCode)) return errorCode; }

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        for ( ; (endSignal==BITv07_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUF144v07_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX2_1(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX2_1(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX2_1(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX2_1(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX2_0(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX2_0(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX2_0(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX2_0(op4, &bitD4);
            endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUF144v07_decodeStreamX2(op1, &bitD1, opStart2, dt, dtLog);
        HUF144v07_decodeStreamX2(op2, &bitD2, opStart3, dt, dtLog);
        HUF144v07_decodeStreamX2(op3, &bitD3, opStart4, dt, dtLog);
        HUF144v07_decodeStreamX2(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        endSignal = BITv07_endOfDStream(&bitD1) & BITv07_endOfDStream(&bitD2) & BITv07_endOfDStream(&bitD3) & BITv07_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruption_detected);

        /* decoded size */
        return dstSize;
    }
}


size_t HUF144v07_decompress4X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
    if (dtd.tableType != 0) return ERROR(GENERIC);
    return HUF144v07_decompress4X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}


size_t HUF144v07_decompress4X2_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF144v07_readDTableX2 (dctx, cSrc, cSrcSize);
    if (HUF144v07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUF144v07_decompress4X2_usingDTable_internal (dst, dstSize, ip, cSrcSize, dctx);
}

size_t HUF144v07_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF144v07_CREATE_STATIC_DTABLEX2(DTable, HUF144v07_TABLELOG_MAX);
    return HUF144v07_decompress4X2_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}


/* *************************/
/* double-symbols decoding */
/* *************************/
typedef struct { U16 sequence; BYTE nbBits; BYTE length; } HUF144v07_DEltX4;  /* double-symbols decoding */

typedef struct { BYTE symbol; BYTE weight; } sortedSymbol_t;

static void HUF144v07_fillDTableX4Level2(HUF144v07_DEltX4* DTable, U32 sizeLog, const U32 consumed,
                           const U32* rankValOrigin, const int minWeight,
                           const sortedSymbol_t* sortedSymbols, const U32 sortedListSize,
                           U32 nbBitsBaseline, U16 baseSeq)
{
    HUF144v07_DEltX4 DElt;
    U32 rankVal[HUF144v07_TABLELOG_ABSOLUTEMAX + 1];

    /* get pre-calculated rankVal */
    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill skipped values */
    if (minWeight>1) {
        U32 i, skipSize = rankVal[minWeight];
        MEM_writeLE16(&(DElt.sequence), baseSeq);
        DElt.nbBits   = (BYTE)(consumed);
        DElt.length   = 1;
        for (i = 0; i < skipSize; i++)
            DTable[i] = DElt;
    }

    /* fill DTable */
    { U32 s; for (s=0; s<sortedListSize; s++) {   /* note : sortedSymbols already skipped */
        const U32 symbol = sortedSymbols[s].symbol;
        const U32 weight = sortedSymbols[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 length = 1 << (sizeLog-nbBits);
        const U32 start = rankVal[weight];
        U32 i = start;
        const U32 end = start + length;

        MEM_writeLE16(&(DElt.sequence), (U16)(baseSeq + (symbol << 8)));
        DElt.nbBits = (BYTE)(nbBits + consumed);
        DElt.length = 2;
        do { DTable[i++] = DElt; } while (i<end);   /* since length >= 1 */

        rankVal[weight] += length;
    }}
}

typedef U32 rankVal_t[HUF144v07_TABLELOG_ABSOLUTEMAX][HUF144v07_TABLELOG_ABSOLUTEMAX + 1];

static void HUF144v07_fillDTableX4(HUF144v07_DEltX4* DTable, const U32 targetLog,
                           const sortedSymbol_t* sortedList, const U32 sortedListSize,
                           const U32* rankStart, rankVal_t rankValOrigin, const U32 maxWeight,
                           const U32 nbBitsBaseline)
{
    U32 rankVal[HUF144v07_TABLELOG_ABSOLUTEMAX + 1];
    const int scaleLog = nbBitsBaseline - targetLog;   /* note : targetLog >= srcLog, hence scaleLog <= 1 */
    const U32 minBits  = nbBitsBaseline - maxWeight;
    U32 s;

    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill DTable */
    for (s=0; s<sortedListSize; s++) {
        const U16 symbol = sortedList[s].symbol;
        const U32 weight = sortedList[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 start = rankVal[weight];
        const U32 length = 1 << (targetLog-nbBits);

        if (targetLog-nbBits >= minBits) {   /* enough room for a second symbol */
            U32 sortedRank;
            int minWeight = nbBits + scaleLog;
            if (minWeight < 1) minWeight = 1;
            sortedRank = rankStart[minWeight];
            HUF144v07_fillDTableX4Level2(DTable+start, targetLog-nbBits, nbBits,
                           rankValOrigin[nbBits], minWeight,
                           sortedList+sortedRank, sortedListSize-sortedRank,
                           nbBitsBaseline, symbol);
        } else {
            HUF144v07_DEltX4 DElt;
            MEM_writeLE16(&(DElt.sequence), symbol);
            DElt.nbBits = (BYTE)(nbBits);
            DElt.length = 1;
            {   U32 u;
                const U32 end = start + length;
                for (u = start; u < end; u++) DTable[u] = DElt;
        }   }
        rankVal[weight] += length;
    }
}

size_t HUF144v07_readDTableX4 (HUF144v07_DTable* DTable, const void* src, size_t srcSize)
{
    BYTE weightList[HUF144v07_SYMBOLVALUE_MAX + 1];
    sortedSymbol_t sortedSymbol[HUF144v07_SYMBOLVALUE_MAX + 1];
    U32 rankStats[HUF144v07_TABLELOG_ABSOLUTEMAX + 1] = { 0 };
    U32 rankStart0[HUF144v07_TABLELOG_ABSOLUTEMAX + 2] = { 0 };
    U32* const rankStart = rankStart0+1;
    rankVal_t rankVal;
    U32 tableLog, maxW, sizeOfSort, nbSymbols;
    DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
    U32 const maxTableLog = dtd.maxTableLog;
    size_t iSize;
    void* dtPtr = DTable+1;   /* force compiler to avoid strict-aliasing */
    HUF144v07_DEltX4* const dt = (HUF144v07_DEltX4*)dtPtr;

    HUF144v07_STATIC_ASSERT(sizeof(HUF144v07_DEltX4) == sizeof(HUF144v07_DTable));   /* if compilation fails here, assertion is false */
    if (maxTableLog > HUF144v07_TABLELOG_ABSOLUTEMAX) return ERROR(tableLog_tooLarge);
    //memset(weightList, 0, sizeof(weightList));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUF144v07_readStats(weightList, HUF144v07_SYMBOLVALUE_MAX + 1, rankStats, &nbSymbols, &tableLog, src, srcSize);
    if (HUF144v07_isError(iSize)) return iSize;

    /* check result */
    if (tableLog > maxTableLog) return ERROR(tableLog_tooLarge);   /* DTable can't fit code depth */

    /* find maxWeight */
    for (maxW = tableLog; rankStats[maxW]==0; maxW--) {}  /* necessarily finds a solution before 0 */

    /* Get start index of each weight */
    {   U32 w, nextRankStart = 0;
        for (w=1; w<maxW+1; w++) {
            U32 current = nextRankStart;
            nextRankStart += rankStats[w];
            rankStart[w] = current;
        }
        rankStart[0] = nextRankStart;   /* put all 0w symbols at the end of sorted list*/
        sizeOfSort = nextRankStart;
    }

    /* sort symbols by weight */
    {   U32 s;
        for (s=0; s<nbSymbols; s++) {
            U32 const w = weightList[s];
            U32 const r = rankStart[w]++;
            sortedSymbol[r].symbol = (BYTE)s;
            sortedSymbol[r].weight = (BYTE)w;
        }
        rankStart[0] = 0;   /* forget 0w symbols; this is beginning of weight(1) */
    }

    /* Build rankVal */
    {   U32* const rankVal0 = rankVal[0];
        {   int const rescale = (maxTableLog-tableLog) - 1;   /* tableLog <= maxTableLog */
            U32 nextRankVal = 0;
            U32 w;
            for (w=1; w<maxW+1; w++) {
                U32 current = nextRankVal;
                nextRankVal += rankStats[w] << (w+rescale);
                rankVal0[w] = current;
        }   }
        {   U32 const minBits = tableLog+1 - maxW;
            U32 consumed;
            for (consumed = minBits; consumed < maxTableLog - minBits + 1; consumed++) {
                U32* const rankValPtr = rankVal[consumed];
                U32 w;
                for (w = 1; w < maxW+1; w++) {
                    rankValPtr[w] = rankVal0[w] >> consumed;
    }   }   }   }

    HUF144v07_fillDTableX4(dt, maxTableLog,
                   sortedSymbol, sizeOfSort,
                   rankStart0, rankVal, maxW,
                   tableLog+1);

    dtd.tableLog = (BYTE)maxTableLog;
    dtd.tableType = 1;
    memcpy(DTable, &dtd, sizeof(dtd));
    return iSize;
}


static U32 HUF144v07_decodeSymbolX4(void* op, BITv07_DStream_t* DStream, const HUF144v07_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BITv07_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 2);
    BITv07_skipBits(DStream, dt[val].nbBits);
    return dt[val].length;
}

static U32 HUF144v07_decodeLastSymbolX4(void* op, BITv07_DStream_t* DStream, const HUF144v07_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BITv07_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 1);
    if (dt[val].length==1) BITv07_skipBits(DStream, dt[val].nbBits);
    else {
        if (DStream->bitsConsumed < (sizeof(DStream->bitContainer)*8)) {
            BITv07_skipBits(DStream, dt[val].nbBits);
            if (DStream->bitsConsumed > (sizeof(DStream->bitContainer)*8))
                DStream->bitsConsumed = (sizeof(DStream->bitContainer)*8);   /* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
    }   }
    return 1;
}


#define HUF144v07_DECODE_SYMBOLX4_0(ptr, DStreamPtr) \
    ptr += HUF144v07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF144v07_DECODE_SYMBOLX4_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUF144v07_TABLELOG_MAX<=12)) \
        ptr += HUF144v07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF144v07_DECODE_SYMBOLX4_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        ptr += HUF144v07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

static inline size_t HUF144v07_decodeStreamX4(BYTE* p, BITv07_DStream_t* bitDPtr, BYTE* const pEnd, const HUF144v07_DEltX4* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 8 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p < pEnd-7)) {
        HUF144v07_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX4_1(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUF144v07_DECODE_SYMBOLX4_0(p, bitDPtr);
    }

    /* closer to end : up to 2 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p <= pEnd-2))
        HUF144v07_DECODE_SYMBOLX4_0(p, bitDPtr);

    while (p <= pEnd-2)
        HUF144v07_DECODE_SYMBOLX4_0(p, bitDPtr);   /* no need to reload : reached the end of DStream */

    if (p < pEnd)
        p += HUF144v07_decodeLastSymbolX4(p, bitDPtr, dt, dtLog);

    return p-pStart;
}


static size_t HUF144v07_decompress1X4_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    BITv07_DStream_t bitD;

    /* Init */
    {   size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);
        if (HUF144v07_isError(errorCode)) return errorCode;
    }

    /* decode */
    {   BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable+1;   /* force compiler to not use strict-aliasing */
        const HUF144v07_DEltX4* const dt = (const HUF144v07_DEltX4*)dtPtr;
        DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
        HUF144v07_decodeStreamX4(ostart, &bitD, oend, dt, dtd.tableLog);
    }

    /* check */
    if (!BITv07_endOfDStream(&bitD)) return ERROR(corruption_detected);

    /* decoded size */
    return dstSize;
}

size_t HUF144v07_decompress1X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
    if (dtd.tableType != 1) return ERROR(GENERIC);
    return HUF144v07_decompress1X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF144v07_decompress1X4_DCtx (HUF144v07_DTable* DCtx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF144v07_readDTableX4 (DCtx, cSrc, cSrcSize);
    if (HUF144v07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUF144v07_decompress1X4_usingDTable_internal (dst, dstSize, ip, cSrcSize, DCtx);
}

size_t HUF144v07_decompress1X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF144v07_CREATE_STATIC_DTABLEX4(DTable, HUF144v07_TABLELOG_MAX);
    return HUF144v07_decompress1X4_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}

static size_t HUF144v07_decompress4X4_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    if (cSrcSize < 10) return ERROR(corruption_detected);   /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable+1;
        const HUF144v07_DEltX4* const dt = (const HUF144v07_DEltX4*)dtPtr;

        /* Init */
        BITv07_DStream_t bitD1;
        BITv07_DStream_t bitD2;
        BITv07_DStream_t bitD3;
        BITv07_DStream_t bitD4;
        size_t const length1 = MEM_readLE16(istart);
        size_t const length2 = MEM_readLE16(istart+2);
        size_t const length3 = MEM_readLE16(istart+4);
        size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        size_t const segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;
        DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
        U32 const dtLog = dtd.tableLog;

        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        { size_t const errorCode = BITv07_initDStream(&bitD1, istart1, length1);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD2, istart2, length2);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD3, istart3, length3);
          if (HUF144v07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD4, istart4, length4);
          if (HUF144v07_isError(errorCode)) return errorCode; }

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        for ( ; (endSignal==BITv07_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUF144v07_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX4_1(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX4_1(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX4_1(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX4_1(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUF144v07_DECODE_SYMBOLX4_0(op1, &bitD1);
            HUF144v07_DECODE_SYMBOLX4_0(op2, &bitD2);
            HUF144v07_DECODE_SYMBOLX4_0(op3, &bitD3);
            HUF144v07_DECODE_SYMBOLX4_0(op4, &bitD4);

            endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUF144v07_decodeStreamX4(op1, &bitD1, opStart2, dt, dtLog);
        HUF144v07_decodeStreamX4(op2, &bitD2, opStart3, dt, dtLog);
        HUF144v07_decodeStreamX4(op3, &bitD3, opStart4, dt, dtLog);
        HUF144v07_decodeStreamX4(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        { U32 const endCheck = BITv07_endOfDStream(&bitD1) & BITv07_endOfDStream(&bitD2) & BITv07_endOfDStream(&bitD3) & BITv07_endOfDStream(&bitD4);
          if (!endCheck) return ERROR(corruption_detected); }

        /* decoded size */
        return dstSize;
    }
}


size_t HUF144v07_decompress4X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUF144v07_DTable* DTable)
{
    DTableDesc dtd = HUF144v07_getDTableDesc(DTable);
    if (dtd.tableType != 1) return ERROR(GENERIC);
    return HUF144v07_decompress4X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}


size_t HUF144v07_decompress4X4_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t hSize = HUF144v07_readDTableX4 (dctx, cSrc, cSrcSize);
    if (HUF144v07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUF144v07_decompress4X4_usingDTable_internal(dst, dstSize, ip, cSrcSize, dctx);
}

size_t HUF144v07_decompress4X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF144v07_CREATE_STATIC_DTABLEX4(DTable, HUF144v07_TABLELOG_MAX);
    return HUF144v07_decompress4X4_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}


/* ********************************/
/* Generic decompression selector */
/* ********************************/

size_t HUF144v07_decompress1X_usingDTable(void* dst, size_t maxDstSize,
                                    const void* cSrc, size_t cSrcSize,
                                    const HUF144v07_DTable* DTable)
{
    DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
    return dtd.tableType ? HUF144v07_decompress1X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable) :
                           HUF144v07_decompress1X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}

size_t HUF144v07_decompress4X_usingDTable(void* dst, size_t maxDstSize,
                                    const void* cSrc, size_t cSrcSize,
                                    const HUF144v07_DTable* DTable)
{
    DTableDesc const dtd = HUF144v07_getDTableDesc(DTable);
    return dtd.tableType ? HUF144v07_decompress4X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable) :
                           HUF144v07_decompress4X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}


typedef struct { U32 tableTime; U32 decode256Time; } algo_time_t;
static const algo_time_t algoTime[16 /* Quantization */][3 /* single, double, quad */] =
{
    /* single, double, quad */
    {{0,0}, {1,1}, {2,2}},  /* Q==0 : impossible */
    {{0,0}, {1,1}, {2,2}},  /* Q==1 : impossible */
    {{  38,130}, {1313, 74}, {2151, 38}},   /* Q == 2 : 12-18% */
    {{ 448,128}, {1353, 74}, {2238, 41}},   /* Q == 3 : 18-25% */
    {{ 556,128}, {1353, 74}, {2238, 47}},   /* Q == 4 : 25-32% */
    {{ 714,128}, {1418, 74}, {2436, 53}},   /* Q == 5 : 32-38% */
    {{ 883,128}, {1437, 74}, {2464, 61}},   /* Q == 6 : 38-44% */
    {{ 897,128}, {1515, 75}, {2622, 68}},   /* Q == 7 : 44-50% */
    {{ 926,128}, {1613, 75}, {2730, 75}},   /* Q == 8 : 50-56% */
    {{ 947,128}, {1729, 77}, {3359, 77}},   /* Q == 9 : 56-62% */
    {{1107,128}, {2083, 81}, {4006, 84}},   /* Q ==10 : 62-69% */
    {{1177,128}, {2379, 87}, {4785, 88}},   /* Q ==11 : 69-75% */
    {{1242,128}, {2415, 93}, {5155, 84}},   /* Q ==12 : 75-81% */
    {{1349,128}, {2644,106}, {5260,106}},   /* Q ==13 : 81-87% */
    {{1455,128}, {2422,124}, {4174,124}},   /* Q ==14 : 87-93% */
    {{ 722,128}, {1891,145}, {1936,146}},   /* Q ==15 : 93-99% */
};

/** HUF144v07_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUF144v07_decompress4X2, 1==HUF144v07_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUF144v07_selectDecoder (size_t dstSize, size_t cSrcSize)
{
    /* decoder timing evaluation */
    U32 const Q = (U32)(cSrcSize * 16 / dstSize);   /* Q < 16 since dstSize > cSrcSize */
    U32 const D256 = (U32)(dstSize >> 8);
    U32 const DTime0 = algoTime[Q][0].tableTime + (algoTime[Q][0].decode256Time * D256);
    U32 DTime1 = algoTime[Q][1].tableTime + (algoTime[Q][1].decode256Time * D256);
    DTime1 += DTime1 >> 3;  /* advantage to algorithm using less memory, for cache eviction */

    return DTime1 < DTime0;
}


typedef size_t (*decompressionAlgo)(void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);

size_t HUF144v07_decompress (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    static const decompressionAlgo decompress[2] = { HUF144v07_decompress4X2, HUF144v07_decompress4X4 };

    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUF144v07_selectDecoder(dstSize, cSrcSize);
        return decompress[algoNb](dst, dstSize, cSrc, cSrcSize);
    }

    //return HUF144v07_decompress4X2(dst, dstSize, cSrc, cSrcSize);   /* multi-streams single-symbol decoding */
    //return HUF144v07_decompress4X4(dst, dstSize, cSrc, cSrcSize);   /* multi-streams double-symbols decoding */
}

size_t HUF144v07_decompress4X_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUF144v07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUF144v07_decompress4X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUF144v07_decompress4X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}

size_t HUF144v07_decompress4X_hufOnly (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if ((cSrcSize >= dstSize) || (cSrcSize <= 1)) return ERROR(corruption_detected);   /* invalid */

    {   U32 const algoNb = HUF144v07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUF144v07_decompress4X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUF144v07_decompress4X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}

size_t HUF144v07_decompress1X_DCtx (HUF144v07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUF144v07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUF144v07_decompress1X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUF144v07_decompress1X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}
/*
    Common functions of Zstd compression library
    Copyright (C) 2015-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net/
*/



/*-****************************************
*  ZSTD Error Management
******************************************/
/*! ZSTD144v07_isError() :
*   tells if a return value is an error code */
unsigned ZSTD144v07_isError(size_t code) { return ERR144_isError(code); }

/*! ZSTD144v07_getErrorName() :
*   provides error code string from function result (useful for debugging) */
const char* ZSTD144v07_getErrorName(size_t code) { return ERR144_getErrorName(code); }



/* **************************************************************
*  ZBUFF Error Management
****************************************************************/
unsigned ZBUFF144v07_isError(size_t errorCode) { return ERR144_isError(errorCode); }

const char* ZBUFF144v07_getErrorName(size_t errorCode) { return ERR144_getErrorName(errorCode); }



static void* ZSTD144v07_defaultAllocFunction(void* opaque, size_t size)
{
    void* address = malloc(size);
    (void)opaque;
    /* printf("alloc %p, %d opaque=%p \n", address, (int)size, opaque); */
    return address;
}

static void ZSTD144v07_defaultFreeFunction(void* opaque, void* address)
{
    (void)opaque;
    /* if (address) printf("free %p opaque=%p \n", address, opaque); */
    free(address);
}
/*
    zstd_internal - common functions to include
    Header File for include
    Copyright (C) 2014-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : https://www.zstd.net
*/
#ifndef ZSTD144v07_CCOMMON_H_MODULE
#define ZSTD144v07_CCOMMON_H_MODULE


/*-*************************************
*  Common macros
***************************************/
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)>(b) ? (a) : (b))


/*-*************************************
*  Common constants
***************************************/
#define ZSTD144v07_OPT_NUM    (1<<12)
#define ZSTD144v07_DICT_MAGIC  0xEC30A437   /* v0.7 */

#define ZSTD144v07_REP_NUM    3
#define ZSTD144v07_REP_INIT   ZSTD144v07_REP_NUM
#define ZSTD144v07_REP_MOVE   (ZSTD144v07_REP_NUM-1)
static const U32 repStartValue[ZSTD144v07_REP_NUM] = { 1, 4, 8 };

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define BIT7 128
#define BIT6  64
#define BIT5  32
#define BIT4  16
#define BIT1   2
#define BIT0   1

#define ZSTD144v07_WINDOWLOG_ABSOLUTEMIN 10
static const size_t ZSTD144v07_fcs_fieldSize[4] = { 0, 2, 4, 8 };
static const size_t ZSTD144v07_did_fieldSize[4] = { 0, 1, 2, 4 };

#define ZSTD144v07_BLOCKHEADERSIZE 3   /* C standard doesn't allow `static const` variable to be init using another `static const` variable */
static const size_t ZSTD144v07_blockHeaderSize = ZSTD144v07_BLOCKHEADERSIZE;
typedef enum { bt_compressed, bt_raw, bt_rle, bt_end } blockType_t;

#define MIN_SEQUENCES_SIZE 1 /* nbSeq==0 */
#define MIN_CBLOCK_SIZE (1 /*litCSize*/ + 1 /* RLE or RAW */ + MIN_SEQUENCES_SIZE /* nbSeq==0 */)   /* for a non-null block */

#define HufLog 12
typedef enum { lbt_huffman, lbt_repeat, lbt_raw, lbt_rle } litBlockType_t;

#define LONGNBSEQ 0x7F00

#define MINMATCH 3
#define EQUAL_READ32 4

#define Litbits  8
#define MaxLit ((1<<Litbits) - 1)
#define MaxML  52
#define MaxLL  35
#define MaxOff 28
#define MaxSeq MAX(MaxLL, MaxML)   /* Assumption : MaxOff < MaxLL,MaxML */
#define MLFSELog    9
#define LLFSELog    9
#define OffFSELog   8

#define FSE144v07_ENCODING_RAW     0
#define FSE144v07_ENCODING_RLE     1
#define FSE144v07_ENCODING_STATIC  2
#define FSE144v07_ENCODING_DYNAMIC 3

#define ZSTD144_CONTENTSIZE_ERROR   (0ULL - 2)

static const U32 LL144_bits[MaxLL+1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9,10,11,12,
                                     13,14,15,16 };
static const S16 LL144_defaultNorm[MaxLL+1] = { 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
                                             2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
                                            -1,-1,-1,-1 };
static const U32 LL144_defaultNormLog = 6;

static const U32 ML144_bits[MaxML+1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9,10,11,
                                     12,13,14,15,16 };
static const S16 ML144_defaultNorm[MaxML+1] = { 1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
                                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,-1,-1,
                                            -1,-1,-1,-1,-1 };
static const U32 ML144_defaultNormLog = 6;

static const S16 OF144_defaultNorm[MaxOff+1] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
                                              1, 1, 1, 1, 1, 1, 1, 1,-1,-1,-1,-1,-1 };
static const U32 OF144_defaultNormLog = 5;


/*-*******************************************
*  Shared functions to include for inlining
*********************************************/
static void ZSTD144v07_copy8(void* dst, const void* src) { memcpy(dst, src, 8); }
#define COPY8(d,s) { ZSTD144v07_copy8(d,s); d+=8; s+=8; }

/*! ZSTD144v07_wildcopy() :
*   custom version of memcpy(), can copy up to 7 bytes too many (8 bytes if length==0) */
#define WILDCOPY_OVERLENGTH 8
MEM_STATIC void ZSTD144v07_wildcopy(void* dst, const void* src, ptrdiff_t length)
{
    const BYTE* ip = (const BYTE*)src;
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + length;
    do
        COPY8(op, ip)
    while (op < oend);
}


/*-*******************************************
*  Private interfaces
*********************************************/
typedef struct ZSTD144v07_stats_s ZSTD144v07_stats_t;

typedef struct {
    U32 off;
    U32 len;
} ZSTD144v07_match_t;

typedef struct {
    U32 price;
    U32 off;
    U32 mlen;
    U32 litlen;
    U32 rep[ZSTD144v07_REP_INIT];
} ZSTD144v07_optimal_t;

struct ZSTD144v07_stats_s { U32 unused; };

typedef struct {
    void* buffer;
    U32*  offsetStart;
    U32*  offset;
    BYTE* offCodeStart;
    BYTE* litStart;
    BYTE* lit;
    U16*  litLengthStart;
    U16*  litLength;
    BYTE* llCodeStart;
    U16*  matchLengthStart;
    U16*  matchLength;
    BYTE* mlCodeStart;
    U32   longLengthID;   /* 0 == no longLength; 1 == Lit.longLength; 2 == Match.longLength; */
    U32   longLengthPos;
    /* opt */
    ZSTD144v07_optimal_t* priceTable;
    ZSTD144v07_match_t* matchTable;
    U32* matchLengthFreq;
    U32* litLengthFreq;
    U32* litFreq;
    U32* offCodeFreq;
    U32  matchLengthSum;
    U32  matchSum;
    U32  litLengthSum;
    U32  litSum;
    U32  offCodeSum;
    U32  log2matchLengthSum;
    U32  log2matchSum;
    U32  log2litLengthSum;
    U32  log2litSum;
    U32  log2offCodeSum;
    U32  factor;
    U32  cachedPrice;
    U32  cachedLitLength;
    const BYTE* cachedLiterals;
    ZSTD144v07_stats_t stats;
} seqStore_t;

void ZSTD144v07_seqToCodes(const seqStore_t* seqStorePtr, size_t const nbSeq);

/* custom memory allocation functions */
static const ZSTD144v07_customMem defaultCustomMem = { ZSTD144v07_defaultAllocFunction, ZSTD144v07_defaultFreeFunction, NULL };

#endif   /* ZSTD144v07_CCOMMON_H_MODULE */
/*
    zstd - standard compression library
    Copyright (C) 2014-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net
*/

/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
 * HEAPMODE :
 * Select how default decompression function ZSTD144v07_decompress() will allocate memory,
 * in memory stack (0), or in memory heap (1, requires malloc())
 */
#ifndef ZSTD144v07_HEAPMODE
#  define ZSTD144v07_HEAPMODE 1
#endif


/*-*******************************************************
*  Compiler specifics
*********************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4324)        /* disable: C4324: padded structure */
#  pragma warning(disable : 4100)        /* disable: C4100: unreferenced formal parameter */
#endif


/*-*************************************
*  Macros
***************************************/
#define ZSTD144v07_isError ERR144_isError   /* for inlining */
#define FSE144v07_isError  ERR144_isError
#define HUF144v07_isError  ERR144_isError


/*_*******************************************************
*  Memory operations
**********************************************************/
static void ZSTD144v07_copy4(void* dst, const void* src) { memcpy(dst, src, 4); }


/*-*************************************************************
*   Context management
***************************************************************/
typedef enum { ZSTDds_getFrameHeaderSize, ZSTDds_decodeFrameHeader,
               ZSTDds_decodeBlockHeader, ZSTDds_decompressBlock,
               ZSTDds_decodeSkippableHeader, ZSTDds_skipFrame } ZSTD144v07_dStage;

struct ZSTD144v07_DCtx_s
{
    FSE144v07_DTable LLTable[FSE144v07_DTABLE_SIZE_U32(LLFSELog)];
    FSE144v07_DTable OffTable[FSE144v07_DTABLE_SIZE_U32(OffFSELog)];
    FSE144v07_DTable MLTable[FSE144v07_DTABLE_SIZE_U32(MLFSELog)];
    HUF144v07_DTable hufTable[HUF144v07_DTABLE_SIZE(HufLog)];  /* can accommodate HUF144v07_decompress4X */
    const void* previousDstEnd;
    const void* base;
    const void* vBase;
    const void* dictEnd;
    size_t expected;
    U32 rep[3];
    ZSTD144v07_frameParams fParams;
    blockType_t bType;   /* used in ZSTD144v07_decompressContinue(), to transfer blockType between header decoding and block decoding stages */
    ZSTD144v07_dStage stage;
    U32 litEntropy;
    U32 fseEntropy;
    XXH_3264_state_t xxhState;
    size_t headerSize;
    U32 dictID;
    const BYTE* litPtr;
    ZSTD144v07_customMem customMem;
    size_t litSize;
    BYTE litBuffer[ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX + WILDCOPY_OVERLENGTH];
    BYTE headerBuffer[ZSTD144v07_FRAMEHEADERSIZE_MAX];
};  /* typedef'd to ZSTD144v07_DCtx within "zstd_static.h" */

int ZSTD144v07_isSkipFrame(ZSTD144v07_DCtx* dctx);

size_t ZSTD144v07_sizeofDCtx (const ZSTD144v07_DCtx* dctx) { return sizeof(*dctx); }

size_t ZSTD144v07_estimateDCtxSize(void) { return sizeof(ZSTD144v07_DCtx); }

size_t ZSTD144v07_decompressBegin(ZSTD144v07_DCtx* dctx)
{
    dctx->expected = ZSTD144v07_frameHeaderSize_min;
    dctx->stage = ZSTDds_getFrameHeaderSize;
    dctx->previousDstEnd = NULL;
    dctx->base = NULL;
    dctx->vBase = NULL;
    dctx->dictEnd = NULL;
    dctx->hufTable[0] = (HUF144v07_DTable)((HufLog)*0x1000001);
    dctx->litEntropy = dctx->fseEntropy = 0;
    dctx->dictID = 0;
    { int i; for (i=0; i<ZSTD144v07_REP_NUM; i++) dctx->rep[i] = repStartValue[i]; }
    return 0;
}

ZSTD144v07_DCtx* ZSTD144v07_createDCtx_advanced(ZSTD144v07_customMem customMem)
{
    ZSTD144v07_DCtx* dctx;

    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    dctx = (ZSTD144v07_DCtx*) customMem.customAlloc(customMem.opaque, sizeof(ZSTD144v07_DCtx));
    if (!dctx) return NULL;
    memcpy(&dctx->customMem, &customMem, sizeof(ZSTD144v07_customMem));
    ZSTD144v07_decompressBegin(dctx);
    return dctx;
}

ZSTD144v07_DCtx* ZSTD144v07_createDCtx(void)
{
    return ZSTD144v07_createDCtx_advanced(defaultCustomMem);
}

size_t ZSTD144v07_freeDCtx(ZSTD144v07_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support free on NULL */
    dctx->customMem.customFree(dctx->customMem.opaque, dctx);
    return 0;   /* reserved as a potential error code in the future */
}

void ZSTD144v07_copyDCtx(ZSTD144v07_DCtx* dstDCtx, const ZSTD144v07_DCtx* srcDCtx)
{
    memcpy(dstDCtx, srcDCtx,
           sizeof(ZSTD144v07_DCtx) - (ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX+WILDCOPY_OVERLENGTH + ZSTD144v07_frameHeaderSize_max));  /* no need to copy workspace */
}


/*-*************************************************************
*   Decompression section
***************************************************************/

/* Frame format description
   Frame Header -  [ Block Header - Block ] - Frame End
   1) Frame Header
      - 4 bytes - Magic Number : ZSTD144v07_MAGICNUMBER (defined within zstd.h)
      - 1 byte  - Frame Descriptor
   2) Block Header
      - 3 bytes, starting with a 2-bits descriptor
                 Uncompressed, Compressed, Frame End, unused
   3) Block
      See Block Format Description
   4) Frame End
      - 3 bytes, compatible with Block Header
*/


/* Frame Header :

   1 byte - FrameHeaderDescription :
   bit 0-1 : dictID (0, 1, 2 or 4 bytes)
   bit 2   : checksumFlag
   bit 3   : reserved (must be zero)
   bit 4   : reserved (unused, can be any value)
   bit 5   : Single Segment (if 1, WindowLog byte is not present)
   bit 6-7 : FrameContentFieldSize (0, 2, 4, or 8)
             if (SkippedWindowLog && !FrameContentFieldsize) FrameContentFieldsize=1;

   Optional : WindowLog (0 or 1 byte)
   bit 0-2 : octal Fractional (1/8th)
   bit 3-7 : Power of 2, with 0 = 1 KB (up to 2 TB)

   Optional : dictID (0, 1, 2 or 4 bytes)
   Automatic adaptation
   0 : no dictID
   1 : 1 - 255
   2 : 256 - 65535
   4 : all other values

   Optional : content size (0, 1, 2, 4 or 8 bytes)
   0 : unknown          (fcfs==0 and swl==0)
   1 : 0-255 bytes      (fcfs==0 and swl==1)
   2 : 256 - 65535+256  (fcfs==1)
   4 : 0 - 4GB-1        (fcfs==2)
   8 : 0 - 16EB-1       (fcfs==3)
*/


/* Compressed Block, format description

   Block = Literal Section - Sequences Section
   Prerequisite : size of (compressed) block, maximum size of regenerated data

   1) Literal Section

   1.1) Header : 1-5 bytes
        flags: 2 bits
            00 compressed by Huff0
            01 unused
            10 is Raw (uncompressed)
            11 is Rle
            Note : using 01 => Huff0 with precomputed table ?
            Note : delta map ? => compressed ?

   1.1.1) Huff0-compressed literal block : 3-5 bytes
            srcSize < 1 KB => 3 bytes (2-2-10-10) => single stream
            srcSize < 1 KB => 3 bytes (2-2-10-10)
            srcSize < 16KB => 4 bytes (2-2-14-14)
            else           => 5 bytes (2-2-18-18)
            big endian convention

   1.1.2) Raw (uncompressed) literal block header : 1-3 bytes
        size :  5 bits: (IS_RAW<<6) + (0<<4) + size
               12 bits: (IS_RAW<<6) + (2<<4) + (size>>8)
                        size&255
               20 bits: (IS_RAW<<6) + (3<<4) + (size>>16)
                        size>>8&255
                        size&255

   1.1.3) Rle (repeated single byte) literal block header : 1-3 bytes
        size :  5 bits: (IS_RLE<<6) + (0<<4) + size
               12 bits: (IS_RLE<<6) + (2<<4) + (size>>8)
                        size&255
               20 bits: (IS_RLE<<6) + (3<<4) + (size>>16)
                        size>>8&255
                        size&255

   1.1.4) Huff0-compressed literal block, using precomputed CTables : 3-5 bytes
            srcSize < 1 KB => 3 bytes (2-2-10-10) => single stream
            srcSize < 1 KB => 3 bytes (2-2-10-10)
            srcSize < 16KB => 4 bytes (2-2-14-14)
            else           => 5 bytes (2-2-18-18)
            big endian convention

        1- CTable available (stored into workspace ?)
        2- Small input (fast heuristic ? Full comparison ? depend on clevel ?)


   1.2) Literal block content

   1.2.1) Huff0 block, using sizes from header
        See Huff0 format

   1.2.2) Huff0 block, using prepared table

   1.2.3) Raw content

   1.2.4) single byte


   2) Sequences section
      TO DO
*/

/** ZSTD144v07_frameHeaderSize() :
*   srcSize must be >= ZSTD144v07_frameHeaderSize_min.
*   @return : size of the Frame Header */
static size_t ZSTD144v07_frameHeaderSize(const void* src, size_t srcSize)
{
    if (srcSize < ZSTD144v07_frameHeaderSize_min) return ERROR(srcSize_wrong);
    {   BYTE const fhd = ((const BYTE*)src)[4];
        U32 const dictID= fhd & 3;
        U32 const directMode = (fhd >> 5) & 1;
        U32 const fcsId = fhd >> 6;
        return ZSTD144v07_frameHeaderSize_min + !directMode + ZSTD144v07_did_fieldSize[dictID] + ZSTD144v07_fcs_fieldSize[fcsId]
                + (directMode && !ZSTD144v07_fcs_fieldSize[fcsId]);
    }
}


/** ZSTD144v07_getFrameParams() :
*   decode Frame Header, or require larger `srcSize`.
*   @return : 0, `fparamsPtr` is correctly filled,
*            >0, `srcSize` is too small, result is expected `srcSize`,
*             or an error code, which can be tested using ZSTD144v07_isError() */
size_t ZSTD144v07_getFrameParams(ZSTD144v07_frameParams* fparamsPtr, const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;

    if (srcSize < ZSTD144v07_frameHeaderSize_min) return ZSTD144v07_frameHeaderSize_min;
    memset(fparamsPtr, 0, sizeof(*fparamsPtr));
    if (MEM_readLE32(src) != ZSTD144v07_MAGICNUMBER) {
        if ((MEM_readLE32(src) & 0xFFFFFFF0U) == ZSTD144v07_MAGIC_SKIPPABLE_START) {
            if (srcSize < ZSTD144v07_skippableHeaderSize) return ZSTD144v07_skippableHeaderSize; /* magic number + skippable frame length */
            fparamsPtr->frameContentSize = MEM_readLE32((const char *)src + 4);
            fparamsPtr->windowSize = 0; /* windowSize==0 means a frame is skippable */
            return 0;
        }
        return ERROR(prefix_unknown);
    }

    /* ensure there is enough `srcSize` to fully read/decode frame header */
    { size_t const fhsize = ZSTD144v07_frameHeaderSize(src, srcSize);
      if (srcSize < fhsize) return fhsize; }

    {   BYTE const fhdByte = ip[4];
        size_t pos = 5;
        U32 const dictIDSizeCode = fhdByte&3;
        U32 const checksumFlag = (fhdByte>>2)&1;
        U32 const directMode = (fhdByte>>5)&1;
        U32 const fcsID = fhdByte>>6;
        U32 const windowSizeMax = 1U << ZSTD144v07_WINDOWLOG_MAX;
        U32 windowSize = 0;
        U32 dictID = 0;
        U64 frameContentSize = 0;
        if ((fhdByte & 0x08) != 0)   /* reserved bits, which must be zero */
            return ERROR(frameParameter_unsupported);
        if (!directMode) {
            BYTE const wlByte = ip[pos++];
            U32 const windowLog = (wlByte >> 3) + ZSTD144v07_WINDOWLOG_ABSOLUTEMIN;
            if (windowLog > ZSTD144v07_WINDOWLOG_MAX)
                return ERROR(frameParameter_unsupported);
            windowSize = (1U << windowLog);
            windowSize += (windowSize >> 3) * (wlByte&7);
        }

        switch(dictIDSizeCode)
        {
            default:   /* impossible */
            case 0 : break;
            case 1 : dictID = ip[pos]; pos++; break;
            case 2 : dictID = MEM_readLE16(ip+pos); pos+=2; break;
            case 3 : dictID = MEM_readLE32(ip+pos); pos+=4; break;
        }
        switch(fcsID)
        {
            default:   /* impossible */
            case 0 : if (directMode) frameContentSize = ip[pos]; break;
            case 1 : frameContentSize = MEM_readLE16(ip+pos)+256; break;
            case 2 : frameContentSize = MEM_readLE32(ip+pos); break;
            case 3 : frameContentSize = MEM_readLE64(ip+pos); break;
        }
        if (!windowSize) windowSize = (U32)frameContentSize;
        if (windowSize > windowSizeMax)
            return ERROR(frameParameter_unsupported);
        fparamsPtr->frameContentSize = frameContentSize;
        fparamsPtr->windowSize = windowSize;
        fparamsPtr->dictID = dictID;
        fparamsPtr->checksumFlag = checksumFlag;
    }
    return 0;
}


/** ZSTD144v07_getDecompressedSize() :
*   compatible with legacy mode
*   @return : decompressed size if known, 0 otherwise
              note : 0 can mean any of the following :
                   - decompressed size is not provided within frame header
                   - frame header unknown / not supported
                   - frame header not completely provided (`srcSize` too small) */
unsigned long long ZSTD144v07_getDecompressedSize(const void* src, size_t srcSize)
{
    ZSTD144v07_frameParams fparams;
    size_t const frResult = ZSTD144v07_getFrameParams(&fparams, src, srcSize);
    if (frResult!=0) return 0;
    return fparams.frameContentSize;
}


/** ZSTD144v07_decodeFrameHeader() :
*   `srcSize` must be the size provided by ZSTD144v07_frameHeaderSize().
*   @return : 0 if success, or an error code, which can be tested using ZSTD144v07_isError() */
static size_t ZSTD144v07_decodeFrameHeader(ZSTD144v07_DCtx* dctx, const void* src, size_t srcSize)
{
    size_t const result = ZSTD144v07_getFrameParams(&(dctx->fParams), src, srcSize);
    if (dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID)) return ERROR(dictionary_wrong);
    if (dctx->fParams.checksumFlag) XXH_3264_reset(&dctx->xxhState, 0);
    return result;
}


typedef struct
{
    blockType_t blockType;
    U32 origSize;
} blockProperties_t;

/*! ZSTD144v07_getcBlockSize() :
*   Provides the size of compressed block from block header `src` */
static size_t ZSTD144v07_getcBlockSize(const void* src, size_t srcSize, blockProperties_t* bpPtr)
{
    const BYTE* const in = (const BYTE* const)src;
    U32 cSize;

    if (srcSize < ZSTD144v07_blockHeaderSize) return ERROR(srcSize_wrong);

    bpPtr->blockType = (blockType_t)((*in) >> 6);
    cSize = in[2] + (in[1]<<8) + ((in[0] & 7)<<16);
    bpPtr->origSize = (bpPtr->blockType == bt_rle) ? cSize : 0;

    if (bpPtr->blockType == bt_end) return 0;
    if (bpPtr->blockType == bt_rle) return 1;
    return cSize;
}


static size_t ZSTD144v07_copyRawBlock(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    if (srcSize > dstCapacity) return ERROR(dstSize_tooSmall);
    memcpy(dst, src, srcSize);
    return srcSize;
}


/*! ZSTD144v07_decodeLiteralsBlock() :
    @return : nb of bytes read from src (< srcSize ) */
static size_t ZSTD144v07_decodeLiteralsBlock(ZSTD144v07_DCtx* dctx,
                          const void* src, size_t srcSize)   /* note : srcSize < BLOCKSIZE */
{
    const BYTE* const istart = (const BYTE*) src;

    if (srcSize < MIN_CBLOCK_SIZE) return ERROR(corruption_detected);

    switch((litBlockType_t)(istart[0]>> 6))
    {
    case lbt_huffman:
        {   size_t litSize, litCSize, singleStream=0;
            U32 lhSize = (istart[0] >> 4) & 3;
            if (srcSize < 5) return ERROR(corruption_detected);   /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need up to 5 for lhSize, + cSize (+nbSeq) */
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                /* 2 - 2 - 10 - 10 */
                lhSize=3;
                singleStream = istart[0] & 16;
                litSize  = ((istart[0] & 15) << 6) + (istart[1] >> 2);
                litCSize = ((istart[1] &  3) << 8) + istart[2];
                break;
            case 2:
                /* 2 - 2 - 14 - 14 */
                lhSize=4;
                litSize  = ((istart[0] & 15) << 10) + (istart[1] << 2) + (istart[2] >> 6);
                litCSize = ((istart[2] & 63) <<  8) + istart[3];
                break;
            case 3:
                /* 2 - 2 - 18 - 18 */
                lhSize=5;
                litSize  = ((istart[0] & 15) << 14) + (istart[1] << 6) + (istart[2] >> 2);
                litCSize = ((istart[2] &  3) << 16) + (istart[3] << 8) + istart[4];
                break;
            }
            if (litSize > ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(corruption_detected);
            if (litCSize + lhSize > srcSize) return ERROR(corruption_detected);

            if (HUF144v07_isError(singleStream ?
                            HUF144v07_decompress1X2_DCtx(dctx->hufTable, dctx->litBuffer, litSize, istart+lhSize, litCSize) :
                            HUF144v07_decompress4X_hufOnly (dctx->hufTable, dctx->litBuffer, litSize, istart+lhSize, litCSize) ))
                return ERROR(corruption_detected);

            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            dctx->litEntropy = 1;
            memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
            return litCSize + lhSize;
        }
    case lbt_repeat:
        {   size_t litSize, litCSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            if (lhSize != 1)  /* only case supported for now : small litSize, single stream */
                return ERROR(corruption_detected);
            if (dctx->litEntropy==0)
                return ERROR(dictionary_corrupted);

            /* 2 - 2 - 10 - 10 */
            lhSize=3;
            litSize  = ((istart[0] & 15) << 6) + (istart[1] >> 2);
            litCSize = ((istart[1] &  3) << 8) + istart[2];
            if (litCSize + lhSize > srcSize) return ERROR(corruption_detected);

            {   size_t const errorCode = HUF144v07_decompress1X4_usingDTable(dctx->litBuffer, litSize, istart+lhSize, litCSize, dctx->hufTable);
                if (HUF144v07_isError(errorCode)) return ERROR(corruption_detected);
            }
            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
            return litCSize + lhSize;
        }
    case lbt_raw:
        {   size_t litSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                lhSize=1;
                litSize = istart[0] & 31;
                break;
            case 2:
                litSize = ((istart[0] & 15) << 8) + istart[1];
                break;
            case 3:
                litSize = ((istart[0] & 15) << 16) + (istart[1] << 8) + istart[2];
                break;
            }

            if (lhSize+litSize+WILDCOPY_OVERLENGTH > srcSize) {  /* risk reading beyond src buffer with wildcopy */
                if (litSize+lhSize > srcSize) return ERROR(corruption_detected);
                memcpy(dctx->litBuffer, istart+lhSize, litSize);
                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                return lhSize+litSize;
            }
            /* direct reference into compressed stream */
            dctx->litPtr = istart+lhSize;
            dctx->litSize = litSize;
            return lhSize+litSize;
        }
    case lbt_rle:
        {   size_t litSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                lhSize = 1;
                litSize = istart[0] & 31;
                break;
            case 2:
                litSize = ((istart[0] & 15) << 8) + istart[1];
                break;
            case 3:
                litSize = ((istart[0] & 15) << 16) + (istart[1] << 8) + istart[2];
                if (srcSize<4) return ERROR(corruption_detected);   /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need lhSize+1 = 4 */
                break;
            }
            if (litSize > ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(corruption_detected);
            memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            return lhSize+1;
        }
    default:
        return ERROR(corruption_detected);   /* impossible */
    }
}


/*! ZSTD144v07_buildSeqTable() :
    @return : nb bytes read from src,
              or an error code if it fails, testable with ZSTD144v07_isError()
*/
static size_t ZSTD144v07_buildSeqTable(FSE144v07_DTable* DTable, U32 type, U32 max, U32 maxLog,
                                 const void* src, size_t srcSize,
                                 const S16* defaultNorm, U32 defaultLog, U32 flagRepeatTable)
{
    switch(type)
    {
    case FSE144v07_ENCODING_RLE :
        if (!srcSize) return ERROR(srcSize_wrong);
        if ( (*(const BYTE*)src) > max) return ERROR(corruption_detected);
        FSE144v07_buildDTable_rle(DTable, *(const BYTE*)src);   /* if *src > max, data is corrupted */
        return 1;
    case FSE144v07_ENCODING_RAW :
        FSE144v07_buildDTable(DTable, defaultNorm, max, defaultLog);
        return 0;
    case FSE144v07_ENCODING_STATIC:
        if (!flagRepeatTable) return ERROR(corruption_detected);
        return 0;
    default :   /* impossible */
    case FSE144v07_ENCODING_DYNAMIC :
        {   U32 tableLog;
            S16 norm[MaxSeq+1];
            size_t const headerSize = FSE144v07_readNCount(norm, &max, &tableLog, src, srcSize);
            if (FSE144v07_isError(headerSize)) return ERROR(corruption_detected);
            if (tableLog > maxLog) return ERROR(corruption_detected);
            FSE144v07_buildDTable(DTable, norm, max, tableLog);
            return headerSize;
    }   }
}


static size_t ZSTD144v07_decodeSeqHeaders(int* nbSeqPtr,
                             FSE144v07_DTable* DTableLL, FSE144v07_DTable* DTableML, FSE144v07_DTable* DTableOffb, U32 flagRepeatTable,
                             const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* const iend = istart + srcSize;
    const BYTE* ip = istart;

    /* check */
    if (srcSize < MIN_SEQUENCES_SIZE) return ERROR(srcSize_wrong);

    /* SeqHead */
    {   int nbSeq = *ip++;
        if (!nbSeq) { *nbSeqPtr=0; return 1; }
        if (nbSeq > 0x7F) {
            if (nbSeq == 0xFF) {
                if (ip+2 > iend) return ERROR(srcSize_wrong);
                nbSeq = MEM_readLE16(ip) + LONGNBSEQ, ip+=2;
            } else {
                if (ip >= iend) return ERROR(srcSize_wrong);
                nbSeq = ((nbSeq-0x80)<<8) + *ip++;
            }
        }
        *nbSeqPtr = nbSeq;
    }

    /* FSE table descriptors */
    if (ip + 4 > iend) return ERROR(srcSize_wrong); /* min : header byte + all 3 are "raw", hence no header, but at least xxLog bits per type */
    {   U32 const LLtype  = *ip >> 6;
        U32 const OFtype = (*ip >> 4) & 3;
        U32 const MLtype  = (*ip >> 2) & 3;
        ip++;

        /* Build DTables */
        {   size_t const llhSize = ZSTD144v07_buildSeqTable(DTableLL, LLtype, MaxLL, LLFSELog, ip, iend-ip, LL144_defaultNorm, LL144_defaultNormLog, flagRepeatTable);
            if (ZSTD144v07_isError(llhSize)) return ERROR(corruption_detected);
            ip += llhSize;
        }
        {   size_t const ofhSize = ZSTD144v07_buildSeqTable(DTableOffb, OFtype, MaxOff, OffFSELog, ip, iend-ip, OF144_defaultNorm, OF144_defaultNormLog, flagRepeatTable);
            if (ZSTD144v07_isError(ofhSize)) return ERROR(corruption_detected);
            ip += ofhSize;
        }
        {   size_t const mlhSize = ZSTD144v07_buildSeqTable(DTableML, MLtype, MaxML, MLFSELog, ip, iend-ip, ML144_defaultNorm, ML144_defaultNormLog, flagRepeatTable);
            if (ZSTD144v07_isError(mlhSize)) return ERROR(corruption_detected);
            ip += mlhSize;
    }   }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t matchLength;
    size_t offset;
} seq_t;

typedef struct {
    BITv07_DStream_t DStream;
    FSE144v07_DState_t stateLL;
    FSE144v07_DState_t stateOffb;
    FSE144v07_DState_t stateML;
    size_t prevOffset[ZSTD144v07_REP_INIT];
} seqState_t;


static seq_t ZSTD144v07_decodeSequence(seqState_t* seqState)
{
    seq_t seq;

    U32 const llCode = FSE144v07_peekSymbol(&(seqState->stateLL));
    U32 const mlCode = FSE144v07_peekSymbol(&(seqState->stateML));
    U32 const ofCode = FSE144v07_peekSymbol(&(seqState->stateOffb));   /* <= maxOff, by table construction */

    U32 const llBits = LL144_bits[llCode];
    U32 const mlBits = ML144_bits[mlCode];
    U32 const ofBits = ofCode;
    U32 const totalBits = llBits+mlBits+ofBits;

    static const U32 LL144_base[MaxLL+1] = {
                             0,  1,  2,  3,  4,  5,  6,  7,  8,  9,   10,    11,    12,    13,    14,     15,
                            16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000,
                            0x2000, 0x4000, 0x8000, 0x10000 };

    static const U32 ML144_base[MaxML+1] = {
                             3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,   14,    15,    16,    17,    18,
                            19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,   30,    31,    32,    33,    34,
                            35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803,
                            0x1003, 0x2003, 0x4003, 0x8003, 0x10003 };

    static const U32 OF144_base[MaxOff+1] = {
                 0,        1,       1,       5,     0xD,     0x1D,     0x3D,     0x7D,
                 0xFD,   0x1FD,   0x3FD,   0x7FD,   0xFFD,   0x1FFD,   0x3FFD,   0x7FFD,
                 0xFFFD, 0x1FFFD, 0x3FFFD, 0x7FFFD, 0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD,
                 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD };

    /* sequence */
    {   size_t offset;
        if (!ofCode)
            offset = 0;
        else {
            offset = OF144_base[ofCode] + BITv07_readBits(&(seqState->DStream), ofBits);   /* <=  (ZSTD144v07_WINDOWLOG_MAX-1) bits */
            if (MEM_32bits()) BITv07_reloadDStream(&(seqState->DStream));
        }

        if (ofCode <= 1) {
            if ((llCode == 0) & (offset <= 1)) offset = 1-offset;
            if (offset) {
                size_t const temp = seqState->prevOffset[offset];
                if (offset != 1) seqState->prevOffset[2] = seqState->prevOffset[1];
                seqState->prevOffset[1] = seqState->prevOffset[0];
                seqState->prevOffset[0] = offset = temp;
            } else {
                offset = seqState->prevOffset[0];
            }
        } else {
            seqState->prevOffset[2] = seqState->prevOffset[1];
            seqState->prevOffset[1] = seqState->prevOffset[0];
            seqState->prevOffset[0] = offset;
        }
        seq.offset = offset;
    }

    seq.matchLength = ML144_base[mlCode] + ((mlCode>31) ? BITv07_readBits(&(seqState->DStream), mlBits) : 0);   /* <=  16 bits */
    if (MEM_32bits() && (mlBits+llBits>24)) BITv07_reloadDStream(&(seqState->DStream));

    seq.litLength = LL144_base[llCode] + ((llCode>15) ? BITv07_readBits(&(seqState->DStream), llBits) : 0);   /* <=  16 bits */
    if (MEM_32bits() ||
       (totalBits > 64 - 7 - (LLFSELog+MLFSELog+OffFSELog)) ) BITv07_reloadDStream(&(seqState->DStream));

    /* ANS state update */
    FSE144v07_updateState(&(seqState->stateLL), &(seqState->DStream));   /* <=  9 bits */
    FSE144v07_updateState(&(seqState->stateML), &(seqState->DStream));   /* <=  9 bits */
    if (MEM_32bits()) BITv07_reloadDStream(&(seqState->DStream));     /* <= 18 bits */
    FSE144v07_updateState(&(seqState->stateOffb), &(seqState->DStream)); /* <=  8 bits */

    return seq;
}


static
size_t ZSTD144v07_execSequence(BYTE* op,
                                BYTE* const oend, seq_t sequence,
                                const BYTE** litPtr, const BYTE* const litLimit,
                                const BYTE* const base, const BYTE* const vBase, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    BYTE* const oMatchEnd = op + sequenceLength;   /* risk : address space overflow (32-bits) */
    BYTE* const oend_w = oend-WILDCOPY_OVERLENGTH;
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;

    /* check */
    if ((oLitEnd>oend_w) | (oMatchEnd>oend)) return ERROR(dstSize_tooSmall); /* last match must start at a minimum distance of WILDCOPY_OVERLENGTH from oend */
    if (iLitEnd > litLimit) return ERROR(corruption_detected);   /* over-read beyond lit buffer */

    /* copy Literals */
    ZSTD144v07_wildcopy(op, *litPtr, sequence.litLength);   /* note : since oLitEnd <= oend-WILDCOPY_OVERLENGTH, no risk of overwrite beyond oend */
    op = oLitEnd;
    *litPtr = iLitEnd;   /* update for next sequence */

    /* copy Match */
    if (sequence.offset > (size_t)(oLitEnd - base)) {
        /* offset beyond prefix */
        if (sequence.offset > (size_t)(oLitEnd - vBase)) return ERROR(corruption_detected);
        match = dictEnd - (base-match);
        if (match + sequence.matchLength <= dictEnd) {
            memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = base;
            if (op > oend_w || sequence.matchLength < MINMATCH) {
              while (op < oMatchEnd) *op++ = *match++;
              return sequenceLength;
            }
    }   }
    /* Requirement: op <= oend_w */

    /* match within prefix */
    if (sequence.offset < 8) {
        /* close range match, overlap */
        static const U32 dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };   /* added */
        static const int dec64table[] = { 8, 8, 8, 7, 8, 9,10,11 };   /* subtracted */
        int const sub2 = dec64table[sequence.offset];
        op[0] = match[0];
        op[1] = match[1];
        op[2] = match[2];
        op[3] = match[3];
        match += dec32table[sequence.offset];
        ZSTD144v07_copy4(op+4, match);
        match -= sub2;
    } else {
        ZSTD144v07_copy8(op, match);
    }
    op += 8; match += 8;

    if (oMatchEnd > oend-(16-MINMATCH)) {
        if (op < oend_w) {
            ZSTD144v07_wildcopy(op, match, oend_w - op);
            match += oend_w - op;
            op = oend_w;
        }
        while (op < oMatchEnd) *op++ = *match++;
    } else {
        ZSTD144v07_wildcopy(op, match, (ptrdiff_t)sequence.matchLength-8);   /* works even if matchLength < 8 */
    }
    return sequenceLength;
}


static size_t ZSTD144v07_decompressSequences(
                               ZSTD144v07_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    FSE144v07_DTable* DTableLL = dctx->LLTable;
    FSE144v07_DTable* DTableML = dctx->MLTable;
    FSE144v07_DTable* DTableOffb = dctx->OffTable;
    const BYTE* const base = (const BYTE*) (dctx->base);
    const BYTE* const vBase = (const BYTE*) (dctx->vBase);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);
    int nbSeq;

    /* Build Decoding Tables */
    {   size_t const seqHSize = ZSTD144v07_decodeSeqHeaders(&nbSeq, DTableLL, DTableML, DTableOffb, dctx->fseEntropy, ip, seqSize);
        if (ZSTD144v07_isError(seqHSize)) return seqHSize;
        ip += seqHSize;
    }

    /* Regen sequences */
    if (nbSeq) {
        seqState_t seqState;
        dctx->fseEntropy = 1;
        { U32 i; for (i=0; i<ZSTD144v07_REP_INIT; i++) seqState.prevOffset[i] = dctx->rep[i]; }
        { size_t const errorCode = BITv07_initDStream(&(seqState.DStream), ip, iend-ip);
          if (ERR144_isError(errorCode)) return ERROR(corruption_detected); }
        FSE144v07_initDState(&(seqState.stateLL), &(seqState.DStream), DTableLL);
        FSE144v07_initDState(&(seqState.stateOffb), &(seqState.DStream), DTableOffb);
        FSE144v07_initDState(&(seqState.stateML), &(seqState.DStream), DTableML);

        for ( ; (BITv07_reloadDStream(&(seqState.DStream)) <= BITv07_DStream_completed) && nbSeq ; ) {
            nbSeq--;
            {   seq_t const sequence = ZSTD144v07_decodeSequence(&seqState);
                size_t const oneSeqSize = ZSTD144v07_execSequence(op, oend, sequence, &litPtr, litEnd, base, vBase, dictEnd);
                if (ZSTD144v07_isError(oneSeqSize)) return oneSeqSize;
                op += oneSeqSize;
        }   }

        /* check if reached exact end */
        if (nbSeq) return ERROR(corruption_detected);
        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTD144v07_REP_INIT; i++) dctx->rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        //if (litPtr > litEnd) return ERROR(corruption_detected);   /* too many literals already used */
        if (lastLLSize > (size_t)(oend-op)) return ERROR(dstSize_tooSmall);
        memcpy(op, litPtr, lastLLSize);
        op += lastLLSize;
    }

    return op-ostart;
}


static void ZSTD144v07_checkContinuity(ZSTD144v07_DCtx* dctx, const void* dst)
{
    if (dst != dctx->previousDstEnd) {   /* not contiguous */
        dctx->dictEnd = dctx->previousDstEnd;
        dctx->vBase = (const char*)dst - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->base));
        dctx->base = dst;
        dctx->previousDstEnd = dst;
    }
}


static size_t ZSTD144v07_decompressBlock_internal(ZSTD144v07_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{   /* blockType == blockCompressed */
    const BYTE* ip = (const BYTE*)src;

    if (srcSize >= ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(srcSize_wrong);

    /* Decode literals sub-block */
    {   size_t const litCSize = ZSTD144v07_decodeLiteralsBlock(dctx, src, srcSize);
        if (ZSTD144v07_isError(litCSize)) return litCSize;
        ip += litCSize;
        srcSize -= litCSize;
    }
    return ZSTD144v07_decompressSequences(dctx, dst, dstCapacity, ip, srcSize);
}


size_t ZSTD144v07_decompressBlock(ZSTD144v07_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{
    size_t dSize;
    ZSTD144v07_checkContinuity(dctx, dst);
    dSize = ZSTD144v07_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
    dctx->previousDstEnd = (char*)dst + dSize;
    return dSize;
}


/** ZSTD144v07_insertBlock() :
    insert `src` block into `dctx` history. Useful to track uncompressed blocks. */
ZSTDLIBv07_API size_t ZSTD144v07_insertBlock(ZSTD144v07_DCtx* dctx, const void* blockStart, size_t blockSize)
{
    ZSTD144v07_checkContinuity(dctx, blockStart);
    dctx->previousDstEnd = (const char*)blockStart + blockSize;
    return blockSize;
}


static size_t ZSTD144v07_generateNxBytes(void* dst, size_t dstCapacity, BYTE byte, size_t length)
{
    if (length > dstCapacity) return ERROR(dstSize_tooSmall);
    memset(dst, byte, length);
    return length;
}


/*! ZSTD144v07_decompressFrame() :
*   `dctx` must be properly initialized */
static size_t ZSTD144v07_decompressFrame(ZSTD144v07_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    const BYTE* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    size_t remainingSize = srcSize;

    /* check */
    if (srcSize < ZSTD144v07_frameHeaderSize_min+ZSTD144v07_blockHeaderSize) return ERROR(srcSize_wrong);

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTD144v07_frameHeaderSize(src, ZSTD144v07_frameHeaderSize_min);
        if (ZSTD144v07_isError(frameHeaderSize)) return frameHeaderSize;
        if (srcSize < frameHeaderSize+ZSTD144v07_blockHeaderSize) return ERROR(srcSize_wrong);
        if (ZSTD144v07_decodeFrameHeader(dctx, src, frameHeaderSize)) return ERROR(corruption_detected);
        ip += frameHeaderSize; remainingSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        size_t decodedSize;
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTD144v07_getcBlockSize(ip, iend-ip, &blockProperties);
        if (ZSTD144v07_isError(cBlockSize)) return cBlockSize;

        ip += ZSTD144v07_blockHeaderSize;
        remainingSize -= ZSTD144v07_blockHeaderSize;
        if (cBlockSize > remainingSize) return ERROR(srcSize_wrong);

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            decodedSize = ZSTD144v07_decompressBlock_internal(dctx, op, oend-op, ip, cBlockSize);
            break;
        case bt_raw :
            decodedSize = ZSTD144v07_copyRawBlock(op, oend-op, ip, cBlockSize);
            break;
        case bt_rle :
            decodedSize = ZSTD144v07_generateNxBytes(op, oend-op, *ip, blockProperties.origSize);
            break;
        case bt_end :
            /* end of frame */
            if (remainingSize) return ERROR(srcSize_wrong);
            decodedSize = 0;
            break;
        default:
            return ERROR(GENERIC);   /* impossible */
        }
        if (blockProperties.blockType == bt_end) break;   /* bt_end */

        if (ZSTD144v07_isError(decodedSize)) return decodedSize;
        if (dctx->fParams.checksumFlag) XXH_3264_update(&dctx->xxhState, op, decodedSize);
        op += decodedSize;
        ip += cBlockSize;
        remainingSize -= cBlockSize;
    }

    return op-ostart;
}


/*! ZSTD144v07_decompress_usingPreparedDCtx() :
*   Same as ZSTD144v07_decompress_usingDict, but using a reference context `preparedDCtx`, where dictionary has been loaded.
*   It avoids reloading the dictionary each time.
*   `preparedDCtx` must have been properly initialized using ZSTD144v07_decompressBegin_usingDict().
*   Requires 2 contexts : 1 for reference (preparedDCtx), which will not be modified, and 1 to run the decompression operation (dctx) */
static size_t ZSTD144v07_decompress_usingPreparedDCtx(ZSTD144v07_DCtx* dctx, const ZSTD144v07_DCtx* refDCtx,
                                         void* dst, size_t dstCapacity,
                                   const void* src, size_t srcSize)
{
    ZSTD144v07_copyDCtx(dctx, refDCtx);
    ZSTD144v07_checkContinuity(dctx, dst);
    return ZSTD144v07_decompressFrame(dctx, dst, dstCapacity, src, srcSize);
}


size_t ZSTD144v07_decompress_usingDict(ZSTD144v07_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize,
                                 const void* dict, size_t dictSize)
{
    ZSTD144v07_decompressBegin_usingDict(dctx, dict, dictSize);
    ZSTD144v07_checkContinuity(dctx, dst);
    return ZSTD144v07_decompressFrame(dctx, dst, dstCapacity, src, srcSize);
}


size_t ZSTD144v07_decompressDCtx(ZSTD144v07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return ZSTD144v07_decompress_usingDict(dctx, dst, dstCapacity, src, srcSize, NULL, 0);
}


size_t ZSTD144v07_decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
#if defined(ZSTD144v07_HEAPMODE) && (ZSTD144v07_HEAPMODE==1)
    size_t regenSize;
    ZSTD144v07_DCtx* const dctx = ZSTD144v07_createDCtx();
    if (dctx==NULL) return ERROR(memory_allocation);
    regenSize = ZSTD144v07_decompressDCtx(dctx, dst, dstCapacity, src, srcSize);
    ZSTD144v07_freeDCtx(dctx);
    return regenSize;
#else   /* stack mode */
    ZSTD144v07_DCtx dctx;
    return ZSTD144v07_decompressDCtx(&dctx, dst, dstCapacity, src, srcSize);
#endif
}

/* ZSTD144_errorFrameSizeInfoLegacy() :
   assumes `cSize` and `dBound` are _not_ NULL */
static void ZSTD144_errorFrameSizeInfoLegacy(size_t* cSize, unsigned long long* dBound, size_t ret)
{
    *cSize = ret;
    *dBound = ZSTD144_CONTENTSIZE_ERROR;
}

void ZSTD144v07_findFrameSizeInfoLegacy(const void *src, size_t srcSize, size_t* cSize, unsigned long long* dBound)
{
    const BYTE* ip = (const BYTE*)src;
    size_t remainingSize = srcSize;
    size_t nbBlocks = 0;

    /* check */
    if (srcSize < ZSTD144v07_frameHeaderSize_min+ZSTD144v07_blockHeaderSize) {
        ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, ERROR(srcSize_wrong));
        return;
    }

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTD144v07_frameHeaderSize(src, srcSize);
        if (ZSTD144v07_isError(frameHeaderSize)) {
            ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, frameHeaderSize);
            return;
        }
        if (MEM_readLE32(src) != ZSTD144v07_MAGICNUMBER) {
            ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, ERROR(prefix_unknown));
            return;
        }
        if (srcSize < frameHeaderSize+ZSTD144v07_blockHeaderSize) {
            ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, ERROR(srcSize_wrong));
            return;
        }
        ip += frameHeaderSize; remainingSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTD144v07_getcBlockSize(ip, remainingSize, &blockProperties);
        if (ZSTD144v07_isError(cBlockSize)) {
            ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, cBlockSize);
            return;
        }

        ip += ZSTD144v07_blockHeaderSize;
        remainingSize -= ZSTD144v07_blockHeaderSize;

        if (blockProperties.blockType == bt_end) break;

        if (cBlockSize > remainingSize) {
            ZSTD144_errorFrameSizeInfoLegacy(cSize, dBound, ERROR(srcSize_wrong));
            return;
        }

        ip += cBlockSize;
        remainingSize -= cBlockSize;
        nbBlocks++;
    }

    *cSize = ip - (const BYTE*)src;
    *dBound = nbBlocks * ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX;
}

/*_******************************
*  Streaming Decompression API
********************************/
size_t ZSTD144v07_nextSrcSizeToDecompress(ZSTD144v07_DCtx* dctx)
{
    return dctx->expected;
}

int ZSTD144v07_isSkipFrame(ZSTD144v07_DCtx* dctx)
{
    return dctx->stage == ZSTDds_skipFrame;
}

/** ZSTD144v07_decompressContinue() :
*   @return : nb of bytes generated into `dst` (necessarily <= `dstCapacity)
*             or an error code, which can be tested using ZSTD144v07_isError() */
size_t ZSTD144v07_decompressContinue(ZSTD144v07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    /* Sanity check */
    if (srcSize != dctx->expected) return ERROR(srcSize_wrong);
    if (dstCapacity) ZSTD144v07_checkContinuity(dctx, dst);

    switch (dctx->stage)
    {
    case ZSTDds_getFrameHeaderSize :
        if (srcSize != ZSTD144v07_frameHeaderSize_min) return ERROR(srcSize_wrong);   /* impossible */
        if ((MEM_readLE32(src) & 0xFFFFFFF0U) == ZSTD144v07_MAGIC_SKIPPABLE_START) {
            memcpy(dctx->headerBuffer, src, ZSTD144v07_frameHeaderSize_min);
            dctx->expected = ZSTD144v07_skippableHeaderSize - ZSTD144v07_frameHeaderSize_min; /* magic number + skippable frame length */
            dctx->stage = ZSTDds_decodeSkippableHeader;
            return 0;
        }
        dctx->headerSize = ZSTD144v07_frameHeaderSize(src, ZSTD144v07_frameHeaderSize_min);
        if (ZSTD144v07_isError(dctx->headerSize)) return dctx->headerSize;
        memcpy(dctx->headerBuffer, src, ZSTD144v07_frameHeaderSize_min);
        if (dctx->headerSize > ZSTD144v07_frameHeaderSize_min) {
            dctx->expected = dctx->headerSize - ZSTD144v07_frameHeaderSize_min;
            dctx->stage = ZSTDds_decodeFrameHeader;
            return 0;
        }
        dctx->expected = 0;   /* not necessary to copy more */
	/* fall-through */
    case ZSTDds_decodeFrameHeader:
        {   size_t result;
            memcpy(dctx->headerBuffer + ZSTD144v07_frameHeaderSize_min, src, dctx->expected);
            result = ZSTD144v07_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize);
            if (ZSTD144v07_isError(result)) return result;
            dctx->expected = ZSTD144v07_blockHeaderSize;
            dctx->stage = ZSTDds_decodeBlockHeader;
            return 0;
        }
    case ZSTDds_decodeBlockHeader:
        {   blockProperties_t bp;
            size_t const cBlockSize = ZSTD144v07_getcBlockSize(src, ZSTD144v07_blockHeaderSize, &bp);
            if (ZSTD144v07_isError(cBlockSize)) return cBlockSize;
            if (bp.blockType == bt_end) {
                if (dctx->fParams.checksumFlag) {
                    U64 const h64 = XXH_3264_digest(&dctx->xxhState);
                    U32 const h32 = (U32)(h64>>11) & ((1<<22)-1);
                    const BYTE* const ip = (const BYTE*)src;
                    U32 const check32 = ip[2] + (ip[1] << 8) + ((ip[0] & 0x3F) << 16);
                    if (check32 != h32) return ERROR(checksum_wrong);
                }
                dctx->expected = 0;
                dctx->stage = ZSTDds_getFrameHeaderSize;
            } else {
                dctx->expected = cBlockSize;
                dctx->bType = bp.blockType;
                dctx->stage = ZSTDds_decompressBlock;
            }
            return 0;
        }
    case ZSTDds_decompressBlock:
        {   size_t rSize;
            switch(dctx->bType)
            {
            case bt_compressed:
                rSize = ZSTD144v07_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
                break;
            case bt_raw :
                rSize = ZSTD144v07_copyRawBlock(dst, dstCapacity, src, srcSize);
                break;
            case bt_rle :
                return ERROR(GENERIC);   /* not yet handled */
                break;
            case bt_end :   /* should never happen (filtered at phase 1) */
                rSize = 0;
                break;
            default:
                return ERROR(GENERIC);   /* impossible */
            }
            dctx->stage = ZSTDds_decodeBlockHeader;
            dctx->expected = ZSTD144v07_blockHeaderSize;
            dctx->previousDstEnd = (char*)dst + rSize;
            if (ZSTD144v07_isError(rSize)) return rSize;
            if (dctx->fParams.checksumFlag) XXH_3264_update(&dctx->xxhState, dst, rSize);
            return rSize;
        }
    case ZSTDds_decodeSkippableHeader:
        {   memcpy(dctx->headerBuffer + ZSTD144v07_frameHeaderSize_min, src, dctx->expected);
            dctx->expected = MEM_readLE32(dctx->headerBuffer + 4);
            dctx->stage = ZSTDds_skipFrame;
            return 0;
        }
    case ZSTDds_skipFrame:
        {   dctx->expected = 0;
            dctx->stage = ZSTDds_getFrameHeaderSize;
            return 0;
        }
    default:
        return ERROR(GENERIC);   /* impossible */
    }
}


static size_t ZSTD144v07_refDictContent(ZSTD144v07_DCtx* dctx, const void* dict, size_t dictSize)
{
    dctx->dictEnd = dctx->previousDstEnd;
    dctx->vBase = (const char*)dict - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->base));
    dctx->base = dict;
    dctx->previousDstEnd = (const char*)dict + dictSize;
    return 0;
}

static size_t ZSTD144v07_loadEntropy(ZSTD144v07_DCtx* dctx, const void* const dict, size_t const dictSize)
{
    const BYTE* dictPtr = (const BYTE*)dict;
    const BYTE* const dictEnd = dictPtr + dictSize;

    {   size_t const hSize = HUF144v07_readDTableX4(dctx->hufTable, dict, dictSize);
        if (HUF144v07_isError(hSize)) return ERROR(dictionary_corrupted);
        dictPtr += hSize;
    }

    {   short offcodeNCount[MaxOff+1];
        U32 offcodeMaxValue=MaxOff, offcodeLog;
        size_t const offcodeHeaderSize = FSE144v07_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd-dictPtr);
        if (FSE144v07_isError(offcodeHeaderSize)) return ERROR(dictionary_corrupted);
        if (offcodeLog > OffFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSE144v07_buildDTable(dctx->OffTable, offcodeNCount, offcodeMaxValue, offcodeLog);
          if (FSE144v07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += offcodeHeaderSize;
    }

    {   short matchlengthNCount[MaxML+1];
        unsigned matchlengthMaxValue = MaxML, matchlengthLog;
        size_t const matchlengthHeaderSize = FSE144v07_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd-dictPtr);
        if (FSE144v07_isError(matchlengthHeaderSize)) return ERROR(dictionary_corrupted);
        if (matchlengthLog > MLFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSE144v07_buildDTable(dctx->MLTable, matchlengthNCount, matchlengthMaxValue, matchlengthLog);
          if (FSE144v07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += matchlengthHeaderSize;
    }

    {   short litlengthNCount[MaxLL+1];
        unsigned litlengthMaxValue = MaxLL, litlengthLog;
        size_t const litlengthHeaderSize = FSE144v07_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd-dictPtr);
        if (FSE144v07_isError(litlengthHeaderSize)) return ERROR(dictionary_corrupted);
        if (litlengthLog > LLFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSE144v07_buildDTable(dctx->LLTable, litlengthNCount, litlengthMaxValue, litlengthLog);
          if (FSE144v07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += litlengthHeaderSize;
    }

    if (dictPtr+12 > dictEnd) return ERROR(dictionary_corrupted);
    dctx->rep[0] = MEM_readLE32(dictPtr+0); if (dctx->rep[0] == 0 || dctx->rep[0] >= dictSize) return ERROR(dictionary_corrupted);
    dctx->rep[1] = MEM_readLE32(dictPtr+4); if (dctx->rep[1] == 0 || dctx->rep[1] >= dictSize) return ERROR(dictionary_corrupted);
    dctx->rep[2] = MEM_readLE32(dictPtr+8); if (dctx->rep[2] == 0 || dctx->rep[2] >= dictSize) return ERROR(dictionary_corrupted);
    dictPtr += 12;

    dctx->litEntropy = dctx->fseEntropy = 1;
    return dictPtr - (const BYTE*)dict;
}

static size_t ZSTD144v07_decompress_insertDictionary(ZSTD144v07_DCtx* dctx, const void* dict, size_t dictSize)
{
    if (dictSize < 8) return ZSTD144v07_refDictContent(dctx, dict, dictSize);
    {   U32 const magic = MEM_readLE32(dict);
        if (magic != ZSTD144v07_DICT_MAGIC) {
            return ZSTD144v07_refDictContent(dctx, dict, dictSize);   /* pure content mode */
    }   }
    dctx->dictID = MEM_readLE32((const char*)dict + 4);

    /* load entropy tables */
    dict = (const char*)dict + 8;
    dictSize -= 8;
    {   size_t const eSize = ZSTD144v07_loadEntropy(dctx, dict, dictSize);
        if (ZSTD144v07_isError(eSize)) return ERROR(dictionary_corrupted);
        dict = (const char*)dict + eSize;
        dictSize -= eSize;
    }

    /* reference dictionary content */
    return ZSTD144v07_refDictContent(dctx, dict, dictSize);
}


size_t ZSTD144v07_decompressBegin_usingDict(ZSTD144v07_DCtx* dctx, const void* dict, size_t dictSize)
{
    { size_t const errorCode = ZSTD144v07_decompressBegin(dctx);
      if (ZSTD144v07_isError(errorCode)) return errorCode; }

    if (dict && dictSize) {
        size_t const errorCode = ZSTD144v07_decompress_insertDictionary(dctx, dict, dictSize);
        if (ZSTD144v07_isError(errorCode)) return ERROR(dictionary_corrupted);
    }

    return 0;
}


struct ZSTD144v07_DDict_s {
    void* dict;
    size_t dictSize;
    ZSTD144v07_DCtx* refContext;
};  /* typedef'd tp ZSTD144v07_CDict within zstd.h */

static ZSTD144v07_DDict* ZSTD144v07_createDDict_advanced(const void* dict, size_t dictSize, ZSTD144v07_customMem customMem)
{
    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    {   ZSTD144v07_DDict* const ddict = (ZSTD144v07_DDict*) customMem.customAlloc(customMem.opaque, sizeof(*ddict));
        void* const dictContent = customMem.customAlloc(customMem.opaque, dictSize);
        ZSTD144v07_DCtx* const dctx = ZSTD144v07_createDCtx_advanced(customMem);

        if (!dictContent || !ddict || !dctx) {
            customMem.customFree(customMem.opaque, dictContent);
            customMem.customFree(customMem.opaque, ddict);
            customMem.customFree(customMem.opaque, dctx);
            return NULL;
        }

        memcpy(dictContent, dict, dictSize);
        {   size_t const errorCode = ZSTD144v07_decompressBegin_usingDict(dctx, dictContent, dictSize);
            if (ZSTD144v07_isError(errorCode)) {
                customMem.customFree(customMem.opaque, dictContent);
                customMem.customFree(customMem.opaque, ddict);
                customMem.customFree(customMem.opaque, dctx);
                return NULL;
        }   }

        ddict->dict = dictContent;
        ddict->dictSize = dictSize;
        ddict->refContext = dctx;
        return ddict;
    }
}

/*! ZSTD144v07_createDDict() :
*   Create a digested dictionary, ready to start decompression without startup delay.
*   `dict` can be released after `ZSTD144v07_DDict` creation */
ZSTD144v07_DDict* ZSTD144v07_createDDict(const void* dict, size_t dictSize)
{
    ZSTD144v07_customMem const allocator = { NULL, NULL, NULL };
    return ZSTD144v07_createDDict_advanced(dict, dictSize, allocator);
}

size_t ZSTD144v07_freeDDict(ZSTD144v07_DDict* ddict)
{
    ZSTD144v07_freeFunction const cFree = ddict->refContext->customMem.customFree;
    void* const opaque = ddict->refContext->customMem.opaque;
    ZSTD144v07_freeDCtx(ddict->refContext);
    cFree(opaque, ddict->dict);
    cFree(opaque, ddict);
    return 0;
}

/*! ZSTD144v07_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Use dictionary without significant overhead. */
ZSTDLIBv07_API size_t ZSTD144v07_decompress_usingDDict(ZSTD144v07_DCtx* dctx,
                                           void* dst, size_t dstCapacity,
                                     const void* src, size_t srcSize,
                                     const ZSTD144v07_DDict* ddict)
{
    return ZSTD144v07_decompress_usingPreparedDCtx(dctx, ddict->refContext,
                                           dst, dstCapacity,
                                           src, srcSize);
}
/*
    Buffered version of Zstd compression library
    Copyright (C) 2015-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net/
*/



/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFF144v07_DCtx object is required to track streaming operations.
*  Use ZBUFF144v07_createDCtx() and ZBUFF144v07_freeDCtx() to create/release resources.
*  Use ZBUFF144v07_decompressInit() to start a new decompression operation,
*   or ZBUFF144v07_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFF144v07_DCtx objects can be re-init multiple times.
*
*  Use ZBUFF144v07_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of @dst will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change @dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency),
*            or 0 when a frame is completely decoded,
*            or an error code, which can be tested using ZBUFF144v07_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144v07_recommendedDInSize() and ZBUFF144v07_recommendedDOutSize()
*  output : ZBUFF144v07_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF144v07_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFF144v07_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/

typedef enum { ZBUFFds_init, ZBUFFds_loadHeader,
               ZBUFFds_read, ZBUFFds_load, ZBUFFds_flush } ZBUFF144v07_dStage;

/* *** Resource management *** */
struct ZBUFF144v07_DCtx_s {
    ZSTD144v07_DCtx* zd;
    ZSTD144v07_frameParams fParams;
    ZBUFF144v07_dStage stage;
    char*  inBuff;
    size_t inBuffSize;
    size_t inPos;
    char*  outBuff;
    size_t outBuffSize;
    size_t outStart;
    size_t outEnd;
    size_t blockSize;
    BYTE headerBuffer[ZSTD144v07_FRAMEHEADERSIZE_MAX];
    size_t lhSize;
    ZSTD144v07_customMem customMem;
};   /* typedef'd to ZBUFF144v07_DCtx within "zstd_buffered.h" */

ZSTDLIBv07_API ZBUFF144v07_DCtx* ZBUFF144v07_createDCtx_advanced(ZSTD144v07_customMem customMem);

ZBUFF144v07_DCtx* ZBUFF144v07_createDCtx(void)
{
    return ZBUFF144v07_createDCtx_advanced(defaultCustomMem);
}

ZBUFF144v07_DCtx* ZBUFF144v07_createDCtx_advanced(ZSTD144v07_customMem customMem)
{
    ZBUFF144v07_DCtx* zbd;

    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    zbd = (ZBUFF144v07_DCtx*)customMem.customAlloc(customMem.opaque, sizeof(ZBUFF144v07_DCtx));
    if (zbd==NULL) return NULL;
    memset(zbd, 0, sizeof(ZBUFF144v07_DCtx));
    memcpy(&zbd->customMem, &customMem, sizeof(ZSTD144v07_customMem));
    zbd->zd = ZSTD144v07_createDCtx_advanced(customMem);
    if (zbd->zd == NULL) { ZBUFF144v07_freeDCtx(zbd); return NULL; }
    zbd->stage = ZBUFFds_init;
    return zbd;
}

size_t ZBUFF144v07_freeDCtx(ZBUFF144v07_DCtx* zbd)
{
    if (zbd==NULL) return 0;   /* support free on null */
    ZSTD144v07_freeDCtx(zbd->zd);
    if (zbd->inBuff) zbd->customMem.customFree(zbd->customMem.opaque, zbd->inBuff);
    if (zbd->outBuff) zbd->customMem.customFree(zbd->customMem.opaque, zbd->outBuff);
    zbd->customMem.customFree(zbd->customMem.opaque, zbd);
    return 0;
}


/* *** Initialization *** */

size_t ZBUFF144v07_decompressInitDictionary(ZBUFF144v07_DCtx* zbd, const void* dict, size_t dictSize)
{
    zbd->stage = ZBUFFds_loadHeader;
    zbd->lhSize = zbd->inPos = zbd->outStart = zbd->outEnd = 0;
    return ZSTD144v07_decompressBegin_usingDict(zbd->zd, dict, dictSize);
}

size_t ZBUFF144v07_decompressInit(ZBUFF144v07_DCtx* zbd)
{
    return ZBUFF144v07_decompressInitDictionary(zbd, NULL, 0);
}


/* internal util function */
MEM_STATIC size_t ZBUFF144v07_limitCopy(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    size_t const length = MIN(dstCapacity, srcSize);
    memcpy(dst, src, length);
    return length;
}


/* *** Decompression *** */

size_t ZBUFF144v07_decompressContinue(ZBUFF144v07_DCtx* zbd,
                                void* dst, size_t* dstCapacityPtr,
                          const void* src, size_t* srcSizePtr)
{
    const char* const istart = (const char*)src;
    const char* const iend = istart + *srcSizePtr;
    const char* ip = istart;
    char* const ostart = (char*)dst;
    char* const oend = ostart + *dstCapacityPtr;
    char* op = ostart;
    U32 notDone = 1;

    while (notDone) {
        switch(zbd->stage)
        {
        case ZBUFFds_init :
            return ERROR(init_missing);

        case ZBUFFds_loadHeader :
            {   size_t const hSize = ZSTD144v07_getFrameParams(&(zbd->fParams), zbd->headerBuffer, zbd->lhSize);
                if (ZSTD144v07_isError(hSize)) return hSize;
                if (hSize != 0) {
                    size_t const toLoad = hSize - zbd->lhSize;   /* if hSize!=0, hSize > zbd->lhSize */
                    if (toLoad > (size_t)(iend-ip)) {   /* not enough input to load full header */
                        memcpy(zbd->headerBuffer + zbd->lhSize, ip, iend-ip);
                        zbd->lhSize += iend-ip;
                        *dstCapacityPtr = 0;
                        return (hSize - zbd->lhSize) + ZSTD144v07_blockHeaderSize;   /* remaining header bytes + next block header */
                    }
                    memcpy(zbd->headerBuffer + zbd->lhSize, ip, toLoad); zbd->lhSize = hSize; ip += toLoad;
                    break;
            }   }

            /* Consume header */
            {   size_t const h1Size = ZSTD144v07_nextSrcSizeToDecompress(zbd->zd);  /* == ZSTD144v07_frameHeaderSize_min */
                size_t const h1Result = ZSTD144v07_decompressContinue(zbd->zd, NULL, 0, zbd->headerBuffer, h1Size);
                if (ZSTD144v07_isError(h1Result)) return h1Result;
                if (h1Size < zbd->lhSize) {   /* long header */
                    size_t const h2Size = ZSTD144v07_nextSrcSizeToDecompress(zbd->zd);
                    size_t const h2Result = ZSTD144v07_decompressContinue(zbd->zd, NULL, 0, zbd->headerBuffer+h1Size, h2Size);
                    if (ZSTD144v07_isError(h2Result)) return h2Result;
            }   }

            zbd->fParams.windowSize = MAX(zbd->fParams.windowSize, 1U << ZSTD144v07_WINDOWLOG_ABSOLUTEMIN);

            /* Frame header instruct buffer sizes */
            {   size_t const blockSize = MIN(zbd->fParams.windowSize, ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX);
                zbd->blockSize = blockSize;
                if (zbd->inBuffSize < blockSize) {
                    zbd->customMem.customFree(zbd->customMem.opaque, zbd->inBuff);
                    zbd->inBuffSize = blockSize;
                    zbd->inBuff = (char*)zbd->customMem.customAlloc(zbd->customMem.opaque, blockSize);
                    if (zbd->inBuff == NULL) return ERROR(memory_allocation);
                }
                {   size_t const neededOutSize = zbd->fParams.windowSize + blockSize + WILDCOPY_OVERLENGTH * 2;
                    if (zbd->outBuffSize < neededOutSize) {
                        zbd->customMem.customFree(zbd->customMem.opaque, zbd->outBuff);
                        zbd->outBuffSize = neededOutSize;
                        zbd->outBuff = (char*)zbd->customMem.customAlloc(zbd->customMem.opaque, neededOutSize);
                        if (zbd->outBuff == NULL) return ERROR(memory_allocation);
            }   }   }
            zbd->stage = ZBUFFds_read;
            /* pass-through */
	    /* fall-through */
        case ZBUFFds_read:
            {   size_t const neededInSize = ZSTD144v07_nextSrcSizeToDecompress(zbd->zd);
                if (neededInSize==0) {  /* end of frame */
                    zbd->stage = ZBUFFds_init;
                    notDone = 0;
                    break;
                }
                if ((size_t)(iend-ip) >= neededInSize) {  /* decode directly from src */
                    const int isSkipFrame = ZSTD144v07_isSkipFrame(zbd->zd);
                    size_t const decodedSize = ZSTD144v07_decompressContinue(zbd->zd,
                        zbd->outBuff + zbd->outStart, (isSkipFrame ? 0 : zbd->outBuffSize - zbd->outStart),
                        ip, neededInSize);
                    if (ZSTD144v07_isError(decodedSize)) return decodedSize;
                    ip += neededInSize;
                    if (!decodedSize && !isSkipFrame) break;   /* this was just a header */
                    zbd->outEnd = zbd->outStart +  decodedSize;
                    zbd->stage = ZBUFFds_flush;
                    break;
                }
                if (ip==iend) { notDone = 0; break; }   /* no more input */
                zbd->stage = ZBUFFds_load;
            }
	    /* fall-through */
        case ZBUFFds_load:
            {   size_t const neededInSize = ZSTD144v07_nextSrcSizeToDecompress(zbd->zd);
                size_t const toLoad = neededInSize - zbd->inPos;   /* should always be <= remaining space within inBuff */
                size_t loadedSize;
                if (toLoad > zbd->inBuffSize - zbd->inPos) return ERROR(corruption_detected);   /* should never happen */
                loadedSize = ZBUFF144v07_limitCopy(zbd->inBuff + zbd->inPos, toLoad, ip, iend-ip);
                ip += loadedSize;
                zbd->inPos += loadedSize;
                if (loadedSize < toLoad) { notDone = 0; break; }   /* not enough input, wait for more */

                /* decode loaded input */
                {  const int isSkipFrame = ZSTD144v07_isSkipFrame(zbd->zd);
                   size_t const decodedSize = ZSTD144v07_decompressContinue(zbd->zd,
                        zbd->outBuff + zbd->outStart, zbd->outBuffSize - zbd->outStart,
                        zbd->inBuff, neededInSize);
                    if (ZSTD144v07_isError(decodedSize)) return decodedSize;
                    zbd->inPos = 0;   /* input is consumed */
                    if (!decodedSize && !isSkipFrame) { zbd->stage = ZBUFFds_read; break; }   /* this was just a header */
                    zbd->outEnd = zbd->outStart +  decodedSize;
                    zbd->stage = ZBUFFds_flush;
                    /* break; */
                    /* pass-through */
                }
	    }
	    /* fall-through */
        case ZBUFFds_flush:
            {   size_t const toFlushSize = zbd->outEnd - zbd->outStart;
                size_t const flushedSize = ZBUFF144v07_limitCopy(op, oend-op, zbd->outBuff + zbd->outStart, toFlushSize);
                op += flushedSize;
                zbd->outStart += flushedSize;
                if (flushedSize == toFlushSize) {
                    zbd->stage = ZBUFFds_read;
                    if (zbd->outStart + zbd->blockSize > zbd->outBuffSize)
                        zbd->outStart = zbd->outEnd = 0;
                    break;
                }
                /* cannot flush everything */
                notDone = 0;
                break;
            }
        default: return ERROR(GENERIC);   /* impossible */
    }   }

    /* result */
    *srcSizePtr = ip-istart;
    *dstCapacityPtr = op-ostart;
    {   size_t nextSrcSizeHint = ZSTD144v07_nextSrcSizeToDecompress(zbd->zd);
        nextSrcSizeHint -= zbd->inPos;   /* already loaded*/
        return nextSrcSizeHint;
    }
}



/* *************************************
*  Tool functions
***************************************/
size_t ZBUFF144v07_recommendedDInSize(void)  { return ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX + ZSTD144v07_blockHeaderSize /* block header size*/ ; }
size_t ZBUFF144v07_recommendedDOutSize(void) { return ZSTD144v07_BLOCKSIZE_ABSOLUTEMAX; }
