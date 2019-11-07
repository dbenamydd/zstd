/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_V01_H_28739879432
#define ZSTD144_V01_H_28739879432

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
ZSTD144v01_decompress() : decompress ZSTD frames compliant with v0.1.x format
    compressedSize : is the exact source size
    maxOriginalSize : is the size of the 'dst' buffer, which must be already allocated.
                      It must be equal or larger than originalSize, otherwise decompression will fail.
    return : the number of bytes decompressed into destination buffer (originalSize)
             or an errorCode if it fails (which can be tested using ZSTD144v01_isError())
*/
size_t ZSTD144v01_decompress( void* dst, size_t maxOriginalSize,
                     const void* src, size_t compressedSize);

 /**
 ZSTD144v01_findFrameSizeInfoLegacy() : get the source length and decompressed bound of a ZSTD frame compliant with v0.1.x format
     srcSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
     cSize (output parameter)  : the number of bytes that would be read to decompress this frame
                                 or an error code if it fails (which can be tested using ZSTD144v01_isError())
     dBound (output parameter) : an upper-bound for the decompressed size of the data in the frame
                                 or ZSTD144_CONTENTSIZE_ERROR if an error occurs

     note : assumes `cSize` and `dBound` are _not_ NULL.
 */
void ZSTD144v01_findFrameSizeInfoLegacy(const void *src, size_t srcSize,
                                     size_t* cSize, unsigned long long* dBound);

/**
ZSTD144v01_isError() : tells if the result of ZSTD144v01_decompress() is an error
*/
unsigned ZSTD144v01_isError(size_t code);


/* *************************************
*  Advanced functions
***************************************/
typedef struct ZSTD144v01_Dctx_s ZSTD144v01_Dctx;
ZSTD144v01_Dctx* ZSTD144v01_createDCtx(void);
size_t ZSTD144v01_freeDCtx(ZSTD144v01_Dctx* dctx);

size_t ZSTD144v01_decompressDCtx(void* ctx,
                              void* dst, size_t maxOriginalSize,
                        const void* src, size_t compressedSize);

/* *************************************
*  Streaming functions
***************************************/
size_t ZSTD144v01_resetDCtx(ZSTD144v01_Dctx* dctx);

size_t ZSTD144v01_nextSrcSizeToDecompress(ZSTD144v01_Dctx* dctx);
size_t ZSTD144v01_decompressContinue(ZSTD144v01_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
/**
  Use above functions alternatively.
  ZSTD144_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD144_decompressContinue().
  ZSTD144_decompressContinue() will use previous data blocks to improve compression if they are located prior to current block.
  Result is the number of bytes regenerated within 'dst'.
  It can be zero, which is not an error; it just means ZSTD144_decompressContinue() has decoded some header.
*/

/* *************************************
*  Prefix - version detection
***************************************/
#define ZSTD144v01_magicNumber   0xFD2FB51E   /* Big Endian version */
#define ZSTD144v01_magicNumberLE 0x1EB52FFD   /* Little Endian version */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_V01_H_28739879432 */
