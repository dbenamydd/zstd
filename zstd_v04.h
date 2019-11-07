/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_V04_H_91868324769238
#define ZSTD144_V04_H_91868324769238

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include <stddef.h>   /* size_t */


/* *************************************
*  Simple one-step function
***************************************/
/**
ZSTD144v04_decompress() : decompress ZSTD frames compliant with v0.4.x format
    compressedSize : is the exact source size
    maxOriginalSize : is the size of the 'dst' buffer, which must be already allocated.
                      It must be equal or larger than originalSize, otherwise decompression will fail.
    return : the number of bytes decompressed into destination buffer (originalSize)
             or an errorCode if it fails (which can be tested using ZSTD144v01_isError())
*/
size_t ZSTD144v04_decompress( void* dst, size_t maxOriginalSize,
                     const void* src, size_t compressedSize);

 /**
 ZSTD144v04_findFrameSizeInfoLegacy() : get the source length and decompressed bound of a ZSTD frame compliant with v0.4.x format
     srcSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
     cSize (output parameter)  : the number of bytes that would be read to decompress this frame
                                 or an error code if it fails (which can be tested using ZSTD144v01_isError())
     dBound (output parameter) : an upper-bound for the decompressed size of the data in the frame
                                 or ZSTD144_CONTENTSIZE_ERROR if an error occurs

    note : assumes `cSize` and `dBound` are _not_ NULL.
 */
 void ZSTD144v04_findFrameSizeInfoLegacy(const void *src, size_t srcSize,
                                      size_t* cSize, unsigned long long* dBound);

/**
ZSTD144v04_isError() : tells if the result of ZSTD144v04_decompress() is an error
*/
unsigned ZSTD144v04_isError(size_t code);


/* *************************************
*  Advanced functions
***************************************/
typedef struct ZSTD144v04_Dctx_s ZSTD144v04_Dctx;
ZSTD144v04_Dctx* ZSTD144v04_createDCtx(void);
size_t ZSTD144v04_freeDCtx(ZSTD144v04_Dctx* dctx);

size_t ZSTD144v04_decompressDCtx(ZSTD144v04_Dctx* dctx,
                              void* dst, size_t maxOriginalSize,
                        const void* src, size_t compressedSize);


/* *************************************
*  Direct Streaming
***************************************/
size_t ZSTD144v04_resetDCtx(ZSTD144v04_Dctx* dctx);

size_t ZSTD144v04_nextSrcSizeToDecompress(ZSTD144v04_Dctx* dctx);
size_t ZSTD144v04_decompressContinue(ZSTD144v04_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
/**
  Use above functions alternatively.
  ZSTD144_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD144_decompressContinue().
  ZSTD144_decompressContinue() will use previous data blocks to improve compression if they are located prior to current block.
  Result is the number of bytes regenerated within 'dst'.
  It can be zero, which is not an error; it just means ZSTD144_decompressContinue() has decoded some header.
*/


/* *************************************
*  Buffered Streaming
***************************************/
typedef struct ZBUFF144v04_DCtx_s ZBUFF144v04_DCtx;
ZBUFF144v04_DCtx* ZBUFF144v04_createDCtx(void);
size_t         ZBUFF144v04_freeDCtx(ZBUFF144v04_DCtx* dctx);

size_t ZBUFF144v04_decompressInit(ZBUFF144v04_DCtx* dctx);
size_t ZBUFF144v04_decompressWithDictionary(ZBUFF144v04_DCtx* dctx, const void* dict, size_t dictSize);

size_t ZBUFF144v04_decompressContinue(ZBUFF144v04_DCtx* dctx, void* dst, size_t* maxDstSizePtr, const void* src, size_t* srcSizePtr);

/** ************************************************
*  Streaming decompression
*
*  A ZBUFF144_DCtx object is required to track streaming operation.
*  Use ZBUFF144_createDCtx() and ZBUFF144_freeDCtx() to create/release resources.
*  Use ZBUFF144_decompressInit() to start a new decompression operation.
*  ZBUFF144_DCtx objects can be reused multiple times.
*
*  Optionally, a reference to a static dictionary can be set, using ZBUFF144_decompressWithDictionary()
*  It must be the same content as the one set during compression phase.
*  Dictionary content must remain accessible during the decompression process.
*
*  Use ZBUFF144_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *maxDstSizePtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *maxDstSizePtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of dst will be overwritten (up to *maxDstSizePtr) at each function call, so save its content if it matters or change dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to improve latency)
*            or 0 when a frame is completely decoded
*            or an error code, which can be tested using ZBUFF144_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144_recommendedDInSize / ZBUFF144_recommendedDOutSize
*  output : ZBUFF144_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when it's decoded.
*  input : ZBUFF144_recommendedDInSize==128Kb+3; just follow indications from ZBUFF144_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* **************************************************/
unsigned ZBUFF144v04_isError(size_t errorCode);
const char* ZBUFF144v04_getErrorName(size_t errorCode);


/** The below functions provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are not compulsory, they just tend to offer better latency */
size_t ZBUFF144v04_recommendedDInSize(void);
size_t ZBUFF144v04_recommendedDOutSize(void);


/* *************************************
*  Prefix - version detection
***************************************/
#define ZSTD144v04_magicNumber 0xFD2FB524   /* v0.4 */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_V04_H_91868324769238 */
