/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144v05_H
#define ZSTD144v05_H

#if defined (__cplusplus)
extern "C" {
#endif

/*-*************************************
*  Dependencies
***************************************/
#include <stddef.h>   /* size_t */
#include "mem.h"      /* U64, U32 */


/* *************************************
*  Simple functions
***************************************/
/*! ZSTD144v05_decompress() :
    `compressedSize` : is the _exact_ size of the compressed blob, otherwise decompression will fail.
    `dstCapacity` must be large enough, equal or larger than originalSize.
    @return : the number of bytes decompressed into `dst` (<= `dstCapacity`),
              or an errorCode if it fails (which can be tested using ZSTD144v05_isError()) */
size_t ZSTD144v05_decompress( void* dst, size_t dstCapacity,
                     const void* src, size_t compressedSize);

 /**
 ZSTD144v05_findFrameSizeInfoLegacy() : get the source length and decompressed bound of a ZSTD frame compliant with v0.5.x format
     srcSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
     cSize (output parameter)  : the number of bytes that would be read to decompress this frame
                                 or an error code if it fails (which can be tested using ZSTD144v01_isError())
     dBound (output parameter) : an upper-bound for the decompressed size of the data in the frame
                                 or ZSTD144_CONTENTSIZE_ERROR if an error occurs

    note : assumes `cSize` and `dBound` are _not_ NULL.
 */
void ZSTD144v05_findFrameSizeInfoLegacy(const void *src, size_t srcSize,
                                     size_t* cSize, unsigned long long* dBound);

/* *************************************
*  Helper functions
***************************************/
/* Error Management */
unsigned    ZSTD144v05_isError(size_t code);          /*!< tells if a `size_t` function result is an error code */
const char* ZSTD144v05_getErrorName(size_t code);     /*!< provides readable string for an error code */


/* *************************************
*  Explicit memory management
***************************************/
/** Decompression context */
typedef struct ZSTD144v05_DCtx_s ZSTD144v05_DCtx;
ZSTD144v05_DCtx* ZSTD144v05_createDCtx(void);
size_t ZSTD144v05_freeDCtx(ZSTD144v05_DCtx* dctx);      /*!< @return : errorCode */

/** ZSTD144v05_decompressDCtx() :
*   Same as ZSTD144v05_decompress(), but requires an already allocated ZSTD144v05_DCtx (see ZSTD144v05_createDCtx()) */
size_t ZSTD144v05_decompressDCtx(ZSTD144v05_DCtx* ctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  Simple Dictionary API
*************************/
/*! ZSTD144v05_decompress_usingDict() :
*   Decompression using a pre-defined Dictionary content (see dictBuilder).
*   Dictionary must be identical to the one used during compression, otherwise regenerated data will be corrupted.
*   Note : dict can be NULL, in which case, it's equivalent to ZSTD144v05_decompressDCtx() */
size_t ZSTD144v05_decompress_usingDict(ZSTD144v05_DCtx* dctx,
                                            void* dst, size_t dstCapacity,
                                      const void* src, size_t srcSize,
                                      const void* dict,size_t dictSize);

/*-************************
*  Advanced Streaming API
***************************/
typedef enum { ZSTD144v05_fast, ZSTD144v05_greedy, ZSTD144v05_lazy, ZSTD144v05_lazy2, ZSTD144v05_btlazy2, ZSTD144v05_opt, ZSTD144v05_btopt } ZSTD144v05_strategy;
typedef struct {
    U64 srcSize;
    U32 windowLog;     /* the only useful information to retrieve */
    U32 contentLog; U32 hashLog; U32 searchLog; U32 searchLength; U32 targetLength; ZSTD144v05_strategy strategy;
} ZSTD144v05_parameters;
size_t ZSTD144v05_getFrameParams(ZSTD144v05_parameters* params, const void* src, size_t srcSize);

size_t ZSTD144v05_decompressBegin_usingDict(ZSTD144v05_DCtx* dctx, const void* dict, size_t dictSize);
void   ZSTD144v05_copyDCtx(ZSTD144v05_DCtx* dstDCtx, const ZSTD144v05_DCtx* srcDCtx);
size_t ZSTD144v05_nextSrcSizeToDecompress(ZSTD144v05_DCtx* dctx);
size_t ZSTD144v05_decompressContinue(ZSTD144v05_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  ZBUFF API
*************************/
typedef struct ZBUFF144v05_DCtx_s ZBUFF144v05_DCtx;
ZBUFF144v05_DCtx* ZBUFF144v05_createDCtx(void);
size_t         ZBUFF144v05_freeDCtx(ZBUFF144v05_DCtx* dctx);

size_t ZBUFF144v05_decompressInit(ZBUFF144v05_DCtx* dctx);
size_t ZBUFF144v05_decompressInitDictionary(ZBUFF144v05_DCtx* dctx, const void* dict, size_t dictSize);

size_t ZBUFF144v05_decompressContinue(ZBUFF144v05_DCtx* dctx,
                                            void* dst, size_t* dstCapacityPtr,
                                      const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression
*
*  A ZBUFF144v05_DCtx object is required to track streaming operations.
*  Use ZBUFF144v05_createDCtx() and ZBUFF144v05_freeDCtx() to create/release resources.
*  Use ZBUFF144v05_decompressInit() to start a new decompression operation,
*   or ZBUFF144v05_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFF144v05_DCtx objects can be reused multiple times.
*
*  Use ZBUFF144v05_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of @dst will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters or change @dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency)
*            or 0 when a frame is completely decoded
*            or an error code, which can be tested using ZBUFF144v05_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144v05_recommendedDInSize() / ZBUFF144v05_recommendedDOutSize()
*  output : ZBUFF144v05_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF144v05_recommendedDInSize==128Kb+3; just follow indications from ZBUFF144v05_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
unsigned ZBUFF144v05_isError(size_t errorCode);
const char* ZBUFF144v05_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, and tend to offer better latency */
size_t ZBUFF144v05_recommendedDInSize(void);
size_t ZBUFF144v05_recommendedDOutSize(void);



/*-*************************************
*  Constants
***************************************/
#define ZSTD144v05_MAGICNUMBER 0xFD2FB525   /* v0.5 */




#if defined (__cplusplus)
}
#endif

#endif  /* ZSTD144v0505_H */
