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
*  NOTES/WARNINGS
******************************************************************/
/* The streaming API defined here is deprecated.
 * Consider migrating towards ZSTD144_compressStream() API in `zstd.h`
 * See 'lib/README.md'.
 *****************************************************************/


#if defined (__cplusplus)
extern "C" {
#endif

#ifndef ZSTD144_BUFFERED_H_23987
#define ZSTD144_BUFFERED_H_23987

/* *************************************
*  Dependencies
***************************************/
#include <stddef.h>      /* size_t */
#include "zstd.h"        /* ZSTD144_CStream, ZSTD144_DStream, ZSTDLIB_API */


/* ***************************************************************
*  Compiler specifics
*****************************************************************/
/* Deprecation warnings */
/* Should these warnings be a problem,
 * it is generally possible to disable them,
 * typically with -Wno-deprecated-declarations for gcc
 * or _CRT_SECURE_NO_WARNINGS in Visual.
 * Otherwise, it's also possible to define ZBUFF144_DISABLE_DEPRECATE_WARNINGS
 */
#ifdef ZBUFF144_DISABLE_DEPRECATE_WARNINGS
#  define ZBUFF144_DEPRECATED(message) ZSTDLIB_API  /* disable deprecation warnings */
#else
#  if defined (__cplusplus) && (__cplusplus >= 201402) /* C++14 or greater */
#    define ZBUFF144_DEPRECATED(message) [[deprecated(message)]] ZSTDLIB_API
#  elif (defined(GNUC) && (GNUC > 4 || (GNUC == 4 && GNUC_MINOR >= 5))) || defined(__clang__)
#    define ZBUFF144_DEPRECATED(message) ZSTDLIB_API __attribute__((deprecated(message)))
#  elif defined(__GNUC__) && (__GNUC__ >= 3)
#    define ZBUFF144_DEPRECATED(message) ZSTDLIB_API __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define ZBUFF144_DEPRECATED(message) ZSTDLIB_API __declspec(deprecated(message))
#  else
#    pragma message("WARNING: You need to implement ZBUFF144_DEPRECATED for this compiler")
#    define ZBUFF144_DEPRECATED(message) ZSTDLIB_API
#  endif
#endif /* ZBUFF144_DISABLE_DEPRECATE_WARNINGS */


/* *************************************
*  Streaming functions
***************************************/
/* This is the easier "buffered" streaming API,
*  using an internal buffer to lift all restrictions on user-provided buffers
*  which can be any size, any place, for both input and output.
*  ZBUFF and ZSTD are 100% interoperable,
*  frames created by one can be decoded by the other one */

typedef ZSTD144_CStream ZBUFF144_CCtx;
ZBUFF144_DEPRECATED("use ZSTD144_createCStream") ZBUFF144_CCtx* ZBUFF144_createCCtx(void);
ZBUFF144_DEPRECATED("use ZSTD144_freeCStream")   size_t      ZBUFF144_freeCCtx(ZBUFF144_CCtx* cctx);

ZBUFF144_DEPRECATED("use ZSTD144_initCStream")           size_t ZBUFF144_compressInit(ZBUFF144_CCtx* cctx, int compressionLevel);
ZBUFF144_DEPRECATED("use ZSTD144_initCStream_usingDict") size_t ZBUFF144_compressInitDictionary(ZBUFF144_CCtx* cctx, const void* dict, size_t dictSize, int compressionLevel);

ZBUFF144_DEPRECATED("use ZSTD144_compressStream") size_t ZBUFF144_compressContinue(ZBUFF144_CCtx* cctx, void* dst, size_t* dstCapacityPtr, const void* src, size_t* srcSizePtr);
ZBUFF144_DEPRECATED("use ZSTD144_flushStream")    size_t ZBUFF144_compressFlush(ZBUFF144_CCtx* cctx, void* dst, size_t* dstCapacityPtr);
ZBUFF144_DEPRECATED("use ZSTD144_endStream")      size_t ZBUFF144_compressEnd(ZBUFF144_CCtx* cctx, void* dst, size_t* dstCapacityPtr);

/*-*************************************************
*  Streaming compression - howto
*
*  A ZBUFF144_CCtx object is required to track streaming operation.
*  Use ZBUFF144_createCCtx() and ZBUFF144_freeCCtx() to create/release resources.
*  ZBUFF144_CCtx objects can be reused multiple times.
*
*  Start by initializing ZBUF_CCtx.
*  Use ZBUFF144_compressInit() to start a new compression operation.
*  Use ZBUFF144_compressInitDictionary() for a compression which requires a dictionary.
*
*  Use ZBUFF144_compressContinue() repetitively to consume input stream.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written within *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present again remaining data.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each call, so save its content if it matters or change @dst .
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's just a hint, to improve latency)
*            or an error code, which can be tested using ZBUFF144_isError().
*
*  At any moment, it's possible to flush whatever data remains within buffer, using ZBUFF144_compressFlush().
*  The nb of bytes written into `dst` will be reported into *dstCapacityPtr.
*  Note that the function cannot output more than *dstCapacityPtr,
*  therefore, some content might still be left into internal buffer if *dstCapacityPtr is too small.
*  @return : nb of bytes still present into internal buffer (0 if it's empty)
*            or an error code, which can be tested using ZBUFF144_isError().
*
*  ZBUFF144_compressEnd() instructs to finish a frame.
*  It will perform a flush and write frame epilogue.
*  The epilogue is required for decoders to consider a frame completed.
*  Similar to ZBUFF144_compressFlush(), it may not be able to output the entire internal buffer content if *dstCapacityPtr is too small.
*  In which case, call again ZBUFF144_compressFlush() to complete the flush.
*  @return : nb of bytes still present into internal buffer (0 if it's empty)
*            or an error code, which can be tested using ZBUFF144_isError().
*
*  Hint : _recommended buffer_ sizes (not compulsory) : ZBUFF144_recommendedCInSize() / ZBUFF144_recommendedCOutSize()
*  input : ZBUFF144_recommendedCInSize==128 KB block size is the internal unit, use this value to reduce intermediate stages (better latency)
*  output : ZBUFF144_recommendedCOutSize==ZSTD144_compressBound(128 KB) + 3 + 3 : ensures it's always possible to write/flush/end a full block. Skip some buffering.
*  By using both, it ensures that input will be entirely consumed, and output will always contain the result, reducing intermediate buffering.
* **************************************************/


typedef ZSTD144_DStream ZBUFF144_DCtx;
ZBUFF144_DEPRECATED("use ZSTD144_createDStream") ZBUFF144_DCtx* ZBUFF144_createDCtx(void);
ZBUFF144_DEPRECATED("use ZSTD144_freeDStream")   size_t      ZBUFF144_freeDCtx(ZBUFF144_DCtx* dctx);

ZBUFF144_DEPRECATED("use ZSTD144_initDStream")           size_t ZBUFF144_decompressInit(ZBUFF144_DCtx* dctx);
ZBUFF144_DEPRECATED("use ZSTD144_initDStream_usingDict") size_t ZBUFF144_decompressInitDictionary(ZBUFF144_DCtx* dctx, const void* dict, size_t dictSize);

ZBUFF144_DEPRECATED("use ZSTD144_decompressStream") size_t ZBUFF144_decompressContinue(ZBUFF144_DCtx* dctx,
                                            void* dst, size_t* dstCapacityPtr,
                                      const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFF144_DCtx object is required to track streaming operations.
*  Use ZBUFF144_createDCtx() and ZBUFF144_freeDCtx() to create/release resources.
*  Use ZBUFF144_decompressInit() to start a new decompression operation,
*   or ZBUFF144_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFF144_DCtx objects can be re-init multiple times.
*
*  Use ZBUFF144_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change `dst`.
*  @return : 0 when a frame is completely decoded and fully flushed,
*            1 when there is still some data left within internal buffer to flush,
*            >1 when more data is expected, with value being a suggested next input size (it's just a hint, which helps latency),
*            or an error code, which can be tested using ZBUFF144_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF144_recommendedDInSize() and ZBUFF144_recommendedDOutSize()
*  output : ZBUFF144_recommendedDOutSize== 128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF144_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFF144_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
ZBUFF144_DEPRECATED("use ZSTD144_isError")      unsigned ZBUFF144_isError(size_t errorCode);
ZBUFF144_DEPRECATED("use ZSTD144_getErrorName") const char* ZBUFF144_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, they tend to offer better latency */
ZBUFF144_DEPRECATED("use ZSTD144_CStreamInSize")  size_t ZBUFF144_recommendedCInSize(void);
ZBUFF144_DEPRECATED("use ZSTD144_CStreamOutSize") size_t ZBUFF144_recommendedCOutSize(void);
ZBUFF144_DEPRECATED("use ZSTD144_DStreamInSize")  size_t ZBUFF144_recommendedDInSize(void);
ZBUFF144_DEPRECATED("use ZSTD144_DStreamOutSize") size_t ZBUFF144_recommendedDOutSize(void);

#endif  /* ZSTD144_BUFFERED_H_23987 */


#ifdef ZBUFF144_STATIC_LINKING_ONLY
#ifndef ZBUFF144_STATIC_H_30298098432
#define ZBUFF144_STATIC_H_30298098432

/* ====================================================================================
 * The definitions in this section are considered experimental.
 * They should never be used in association with a dynamic library, as they may change in the future.
 * They are provided for advanced usages.
 * Use them only in association with static linking.
 * ==================================================================================== */

/*--- Dependency ---*/
#define ZSTD144_STATIC_LINKING_ONLY   /* ZSTD144_parameters, ZSTD144_customMem */
#include "zstd.h"


/*--- Custom memory allocator ---*/
/*! ZBUFF144_createCCtx_advanced() :
 *  Create a ZBUFF compression context using external alloc and free functions */
ZBUFF144_DEPRECATED("use ZSTD144_createCStream_advanced") ZBUFF144_CCtx* ZBUFF144_createCCtx_advanced(ZSTD144_customMem customMem);

/*! ZBUFF144_createDCtx_advanced() :
 *  Create a ZBUFF decompression context using external alloc and free functions */
ZBUFF144_DEPRECATED("use ZSTD144_createDStream_advanced") ZBUFF144_DCtx* ZBUFF144_createDCtx_advanced(ZSTD144_customMem customMem);


/*--- Advanced Streaming Initialization ---*/
ZBUFF144_DEPRECATED("use ZSTD144_initDStream_usingDict") size_t ZBUFF144_compressInit_advanced(ZBUFF144_CCtx* zbc,
                                               const void* dict, size_t dictSize,
                                               ZSTD144_parameters params, unsigned long long pledgedSrcSize);


#endif    /* ZBUFF144_STATIC_H_30298098432 */
#endif    /* ZBUFF144_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif
