/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144v07_H_235446
#define ZSTD144v07_H_235446

#if defined (__cplusplus)
extern "C" {
#endif

/*======  Dependency  ======*/
#include <stddef.h>   /* size_t */


/*======  Export for Windows  ======*/
/*!
*  ZSTD144v07_DLL144_EXPORT :
*  Enable exporting of functions when building a Windows DLL
*/
#if defined(_WIN32) && defined(ZSTD144v07_DLL144_EXPORT) && (ZSTD144v07_DLL144_EXPORT==1)
#  define ZSTDLIBv07_API __declspec(dllexport)
#else
#  define ZSTDLIBv07_API
#endif


/* *************************************
*  Simple API
***************************************/
/*! ZSTD144v07_getDecompressedSize() :
*   @return : decompressed size if known, 0 otherwise.
       note 1 : if `0`, follow up with ZSTD144v07_getFrameParams() to know precise failure cause.
       note 2 : decompressed size could be wrong or intentionally modified !
                always ensure results fit within application's authorized limits */
unsigned long long ZSTD144v07_getDecompressedSize(const void* src, size_t srcSize);

/*! ZSTD144v07_decompress() :
    `compressedSize` : must be _exact_ size of compressed input, otherwise decompression will fail.
    `dstCapacity` must be equal or larger than originalSize.
    @return : the number of bytes decompressed into `dst` (<= `dstCapacity`),
              or an errorCode if it fails (which can be tested using ZSTD144v07_isError()) */
ZSTDLIBv07_API size_t ZSTD144v07_decompress( void* dst, size_t dstCapacity,
                                    const void* src, size_t compressedSize);

/**
ZSTD144v07_findFrameSizeInfoLegacy() : get the source length and decompressed bound of a ZSTD frame compliant with v0.7.x format
    srcSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
    cSize (output parameter)  : the number of bytes that would be read to decompress this frame
                                or an error code if it fails (which can be tested using ZSTD144v01_isError())
    dBound (output parameter) : an upper-bound for the decompressed size of the data in the frame
                                or ZSTD144_CONTENTSIZE_ERROR if an error occurs

    note : assumes `cSize` and `dBound` are _not_ NULL.
*/
void ZSTD144v07_findFrameSizeInfoLegacy(const void *src, size_t srcSize,
                                     size_t* cSize, unsigned long long* dBound);

/*======  Helper functions  ======*/
ZSTDLIBv07_API unsigned    ZSTD144v07_isError(size_t code);          /*!< tells if a `size_t` function result is an error code */
ZSTDLIBv07_API const char* ZSTD144v07_getErrorName(size_t code);     /*!< provides readable string from an error code */


/*-*************************************
*  Explicit memory management
***************************************/
/** Decompression context */
typedef struct ZSTD144v07_DCtx_s ZSTD144v07_DCtx;
ZSTDLIBv07_API ZSTD144v07_DCtx* ZSTD144v07_createDCtx(void);
ZSTDLIBv07_API size_t     ZSTD144v07_freeDCtx(ZSTD144v07_DCtx* dctx);      /*!< @return : errorCode */

/** ZSTD144v07_decompressDCtx() :
*   Same as ZSTD144v07_decompress(), requires an allocated ZSTD144v07_DCtx (see ZSTD144v07_createDCtx()) */
ZSTDLIBv07_API size_t ZSTD144v07_decompressDCtx(ZSTD144v07_DCtx* ctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-************************
*  Simple dictionary API
***************************/
/*! ZSTD144v07_decompress_usingDict() :
*   Decompression using a pre-defined Dictionary content (see dictBuilder).
*   Dictionary must be identical to the one used during compression.
*   Note : This function load the dictionary, resulting in a significant startup time */
ZSTDLIBv07_API size_t ZSTD144v07_decompress_usingDict(ZSTD144v07_DCtx* dctx,
                                                   void* dst, size_t dstCapacity,
                                             const void* src, size_t srcSize,
                                             const void* dict,size_t dictSize);


/*-**************************
*  Advanced Dictionary API
****************************/
/*! ZSTD144v07_createDDict() :
*   Create a digested dictionary, ready to start decompression operation without startup delay.
*   `dict` can be released after creation */
typedef struct ZSTD144v07_DDict_s ZSTD144v07_DDict;
ZSTDLIBv07_API ZSTD144v07_DDict* ZSTD144v07_createDDict(const void* dict, size_t dictSize);
ZSTDLIBv07_API size_t      ZSTD144v07_freeDDict(ZSTD144v07_DDict* ddict);

/*! ZSTD144v07_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Faster startup than ZSTD144v07_decompress_usingDict(), recommended when same dictionary is used multiple times. */
ZSTDLIBv07_API size_t ZSTD144v07_decompress_usingDDict(ZSTD144v07_DCtx* dctx,
                                                    void* dst, size_t dstCapacity,
                                              const void* src, size_t srcSize,
                                              const ZSTD144v07_DDict* ddict);

typedef struct {
    unsigned long long frameContentSize;
    unsigned windowSize;
    unsigned dictID;
    unsigned checksumFlag;
} ZSTD144v07_frameParams;

ZSTDLIBv07_API size_t ZSTD144v07_getFrameParams(ZSTD144v07_frameParams* fparamsPtr, const void* src, size_t srcSize);   /**< doesn't consume input */




/* *************************************
*  Streaming functions
***************************************/
typedef struct ZBUFF144v07_DCtx_s ZBUFF144v07_DCtx;
ZSTDLIBv07_API ZBUFF144v07_DCtx* ZBUFF144v07_createDCtx(void);
ZSTDLIBv07_API size_t      ZBUFF144v07_freeDCtx(ZBUFF144v07_DCtx* dctx);

ZSTDLIBv07_API size_t ZBUFF144v07_decompressInit(ZBUFF144v07_DCtx* dctx);
ZSTDLIBv07_API size_t ZBUFF144v07_decompressInitDictionary(ZBUFF144v07_DCtx* dctx, const void* dict, size_t dictSize);

ZSTDLIBv07_API size_t ZBUFF144v07_decompressContinue(ZBUFF144v07_DCtx* dctx,
                                            void* dst, size_t* dstCapacityPtr,
                                      const void* src, size_t* srcSizePtr);

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
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change `dst`.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency),
*            or 0 when a frame is completely decoded,
*            or an error code, which can be tested using ZBUFF144v07_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144v07_recommendedDInSize() and ZBUFF144v07_recommendedDOutSize()
*  output : ZBUFF144v07_recommendedDOutSize== 128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF144v07_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFF144v07_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
ZSTDLIBv07_API unsigned ZBUFF144v07_isError(size_t errorCode);
ZSTDLIBv07_API const char* ZBUFF144v07_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, they tend to offer better latency */
ZSTDLIBv07_API size_t ZBUFF144v07_recommendedDInSize(void);
ZSTDLIBv07_API size_t ZBUFF144v07_recommendedDOutSize(void);


/*-*************************************
*  Constants
***************************************/
#define ZSTD144v07_MAGICNUMBER            0xFD2FB527   /* v0.7 */


#if defined (__cplusplus)
}
#endif

#endif  /* ZSTD144v07_H_235446 */
