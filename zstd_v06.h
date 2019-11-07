/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144v06_H
#define ZSTD144v06_H

#if defined (__cplusplus)
extern "C" {
#endif

/*======  Dependency  ======*/
#include <stddef.h>   /* size_t */


/*======  Export for Windows  ======*/
/*!
*  ZSTD144v06_DLL144_EXPORT :
*  Enable exporting of functions when building a Windows DLL
*/
#if defined(_WIN32) && defined(ZSTD144v06_DLL144_EXPORT) && (ZSTD144v06_DLL144_EXPORT==1)
#  define ZSTDLIBv06_API __declspec(dllexport)
#else
#  define ZSTDLIBv06_API
#endif


/* *************************************
*  Simple functions
***************************************/
/*! ZSTD144v06_decompress() :
    `compressedSize` : is the _exact_ size of the compressed blob, otherwise decompression will fail.
    `dstCapacity` must be large enough, equal or larger than originalSize.
    @return : the number of bytes decompressed into `dst` (<= `dstCapacity`),
              or an errorCode if it fails (which can be tested using ZSTD144v06_isError()) */
ZSTDLIBv06_API size_t ZSTD144v06_decompress( void* dst, size_t dstCapacity,
                                    const void* src, size_t compressedSize);

/**
ZSTD144v06_findFrameSizeInfoLegacy() : get the source length and decompressed bound of a ZSTD frame compliant with v0.6.x format
    srcSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
    cSize (output parameter)  : the number of bytes that would be read to decompress this frame
                                or an error code if it fails (which can be tested using ZSTD144v01_isError())
    dBound (output parameter) : an upper-bound for the decompressed size of the data in the frame
                                or ZSTD144_CONTENTSIZE_ERROR if an error occurs

    note : assumes `cSize` and `dBound` are _not_ NULL.
*/
void ZSTD144v06_findFrameSizeInfoLegacy(const void *src, size_t srcSize,
                                     size_t* cSize, unsigned long long* dBound);

/* *************************************
*  Helper functions
***************************************/
ZSTDLIBv06_API size_t      ZSTD144v06_compressBound(size_t srcSize); /*!< maximum compressed size (worst case scenario) */

/* Error Management */
ZSTDLIBv06_API unsigned    ZSTD144v06_isError(size_t code);          /*!< tells if a `size_t` function result is an error code */
ZSTDLIBv06_API const char* ZSTD144v06_getErrorName(size_t code);     /*!< provides readable string for an error code */


/* *************************************
*  Explicit memory management
***************************************/
/** Decompression context */
typedef struct ZSTD144v06_DCtx_s ZSTD144v06_DCtx;
ZSTDLIBv06_API ZSTD144v06_DCtx* ZSTD144v06_createDCtx(void);
ZSTDLIBv06_API size_t     ZSTD144v06_freeDCtx(ZSTD144v06_DCtx* dctx);      /*!< @return : errorCode */

/** ZSTD144v06_decompressDCtx() :
*   Same as ZSTD144v06_decompress(), but requires an already allocated ZSTD144v06_DCtx (see ZSTD144v06_createDCtx()) */
ZSTDLIBv06_API size_t ZSTD144v06_decompressDCtx(ZSTD144v06_DCtx* ctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  Dictionary API
*************************/
/*! ZSTD144v06_decompress_usingDict() :
*   Decompression using a pre-defined Dictionary content (see dictBuilder).
*   Dictionary must be identical to the one used during compression, otherwise regenerated data will be corrupted.
*   Note : dict can be NULL, in which case, it's equivalent to ZSTD144v06_decompressDCtx() */
ZSTDLIBv06_API size_t ZSTD144v06_decompress_usingDict(ZSTD144v06_DCtx* dctx,
                                                   void* dst, size_t dstCapacity,
                                             const void* src, size_t srcSize,
                                             const void* dict,size_t dictSize);


/*-************************
*  Advanced Streaming API
***************************/
struct ZSTD144v06_frameParams_s { unsigned long long frameContentSize; unsigned windowLog; };
typedef struct ZSTD144v06_frameParams_s ZSTD144v06_frameParams;

ZSTDLIBv06_API size_t ZSTD144v06_getFrameParams(ZSTD144v06_frameParams* fparamsPtr, const void* src, size_t srcSize);   /**< doesn't consume input */
ZSTDLIBv06_API size_t ZSTD144v06_decompressBegin_usingDict(ZSTD144v06_DCtx* dctx, const void* dict, size_t dictSize);
ZSTDLIBv06_API void   ZSTD144v06_copyDCtx(ZSTD144v06_DCtx* dctx, const ZSTD144v06_DCtx* preparedDCtx);

ZSTDLIBv06_API size_t ZSTD144v06_nextSrcSizeToDecompress(ZSTD144v06_DCtx* dctx);
ZSTDLIBv06_API size_t ZSTD144v06_decompressContinue(ZSTD144v06_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);



/* *************************************
*  ZBUFF API
***************************************/

typedef struct ZBUFF144v06_DCtx_s ZBUFF144v06_DCtx;
ZSTDLIBv06_API ZBUFF144v06_DCtx* ZBUFF144v06_createDCtx(void);
ZSTDLIBv06_API size_t         ZBUFF144v06_freeDCtx(ZBUFF144v06_DCtx* dctx);

ZSTDLIBv06_API size_t ZBUFF144v06_decompressInit(ZBUFF144v06_DCtx* dctx);
ZSTDLIBv06_API size_t ZBUFF144v06_decompressInitDictionary(ZBUFF144v06_DCtx* dctx, const void* dict, size_t dictSize);

ZSTDLIBv06_API size_t ZBUFF144v06_decompressContinue(ZBUFF144v06_DCtx* dctx,
                                                  void* dst, size_t* dstCapacityPtr,
                                            const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFF144v06_DCtx object is required to track streaming operations.
*  Use ZBUFF144v06_createDCtx() and ZBUFF144v06_freeDCtx() to create/release resources.
*  Use ZBUFF144v06_decompressInit() to start a new decompression operation,
*   or ZBUFF144v06_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFF144v06_DCtx objects can be re-init multiple times.
*
*  Use ZBUFF144v06_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change `dst`.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency),
*            or 0 when a frame is completely decoded,
*            or an error code, which can be tested using ZBUFF144v06_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144v06_recommendedDInSize() and ZBUFF144v06_recommendedDOutSize()
*  output : ZBUFF144v06_recommendedDOutSize== 128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF144v06_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFF144v06_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
ZSTDLIBv06_API unsigned ZBUFF144v06_isError(size_t errorCode);
ZSTDLIBv06_API const char* ZBUFF144v06_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, they tend to offer better latency */
ZSTDLIBv06_API size_t ZBUFF144v06_recommendedDInSize(void);
ZSTDLIBv06_API size_t ZBUFF144v06_recommendedDOutSize(void);


/*-*************************************
*  Constants
***************************************/
#define ZSTD144v06_MAGICNUMBER 0xFD2FB526   /* v0.6 */



#if defined (__cplusplus)
}
#endif

#endif  /* ZSTD144v06_BUFFERED_H */
