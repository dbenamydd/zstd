/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

 #ifndef ZSTDMT144_COMPRESS_H
 #define ZSTDMT144_COMPRESS_H

 #if defined (__cplusplus)
 extern "C" {
 #endif


/* Note : This is an internal API.
 *        These APIs used to be exposed with ZSTDLIB_API,
 *        because it used to be the only way to invoke MT compression.
 *        Now, it's recommended to use ZSTD144_compress2 and ZSTD144_compressStream2()
 *        instead.
 *
 *        If you depend on these APIs and can't switch, then define
 *        ZSTD144_LEGACY_MULTITHREADED_API when making the dynamic library.
 *        However, we may completely remove these functions in a future
 *        release, so please switch soon.
 *
 *        This API requires ZSTD144_MULTITHREAD to be defined during compilation,
 *        otherwise ZSTDMT144_createCCtx*() will fail.
 */

#ifdef ZSTD144_LEGACY_MULTITHREADED_API
#  define ZSTDMT144_API ZSTDLIB_API
#else
#  define ZSTDMT144_API
#endif

/* ===   Dependencies   === */
#include <stddef.h>                /* size_t */
#define ZSTD144_STATIC_LINKING_ONLY   /* ZSTD144_parameters */
#include "zstd.h"            /* ZSTD144_inBuffer, ZSTD144_outBuffer, ZSTDLIB_API */


/* ===   Constants   === */
#ifndef ZSTDMT144_NBWORKERS_MAX
#  define ZSTDMT144_NBWORKERS_MAX 200
#endif
#ifndef ZSTDMT144_JOBSIZE_MIN
#  define ZSTDMT144_JOBSIZE_MIN (1 MB)
#endif
#define ZSTDMT144_JOBLOG_MAX   (MEM_32bits() ? 29 : 30)
#define ZSTDMT144_JOBSIZE_MAX  (MEM_32bits() ? (512 MB) : (1024 MB))


/* ===   Memory management   === */
typedef struct ZSTDMT144_CCtx_s ZSTDMT144_CCtx;
/* Requires ZSTD144_MULTITHREAD to be defined during compilation, otherwise it will return NULL. */
ZSTDMT144_API ZSTDMT144_CCtx* ZSTDMT144_createCCtx(unsigned nbWorkers);
/* Requires ZSTD144_MULTITHREAD to be defined during compilation, otherwise it will return NULL. */
ZSTDMT144_API ZSTDMT144_CCtx* ZSTDMT144_createCCtx_advanced(unsigned nbWorkers,
                                                    ZSTD144_customMem cMem);
ZSTDMT144_API size_t ZSTDMT144_freeCCtx(ZSTDMT144_CCtx* mtctx);

ZSTDMT144_API size_t ZSTDMT144_sizeof_CCtx(ZSTDMT144_CCtx* mtctx);


/* ===   Simple one-pass compression function   === */

ZSTDMT144_API size_t ZSTDMT144_compressCCtx(ZSTDMT144_CCtx* mtctx,
                                       void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize,
                                       int compressionLevel);



/* ===   Streaming functions   === */

ZSTDMT144_API size_t ZSTDMT144_initCStream(ZSTDMT144_CCtx* mtctx, int compressionLevel);
ZSTDMT144_API size_t ZSTDMT144_resetCStream(ZSTDMT144_CCtx* mtctx, unsigned long long pledgedSrcSize);  /**< if srcSize is not known at reset time, use ZSTD144_CONTENTSIZE_UNKNOWN. Note: for compatibility with older programs, 0 means the same as ZSTD144_CONTENTSIZE_UNKNOWN, but it will change in the future to mean "empty" */

ZSTDMT144_API size_t ZSTDMT144_nextInputSizeHint(const ZSTDMT144_CCtx* mtctx);
ZSTDMT144_API size_t ZSTDMT144_compressStream(ZSTDMT144_CCtx* mtctx, ZSTD144_outBuffer* output, ZSTD144_inBuffer* input);

ZSTDMT144_API size_t ZSTDMT144_flushStream(ZSTDMT144_CCtx* mtctx, ZSTD144_outBuffer* output);   /**< @return : 0 == all flushed; >0 : still some data to be flushed; or an error code (ZSTD144_isError()) */
ZSTDMT144_API size_t ZSTDMT144_endStream(ZSTDMT144_CCtx* mtctx, ZSTD144_outBuffer* output);     /**< @return : 0 == all flushed; >0 : still some data to be flushed; or an error code (ZSTD144_isError()) */


/* ===   Advanced functions and parameters  === */

ZSTDMT144_API size_t ZSTDMT144_compress_advanced(ZSTDMT144_CCtx* mtctx,
                                          void* dst, size_t dstCapacity,
                                    const void* src, size_t srcSize,
                                    const ZSTD144_CDict* cdict,
                                          ZSTD144_parameters params,
                                          int overlapLog);

ZSTDMT144_API size_t ZSTDMT144_initCStream_advanced(ZSTDMT144_CCtx* mtctx,
                                        const void* dict, size_t dictSize,   /* dict can be released after init, a local copy is preserved within zcs */
                                        ZSTD144_parameters params,
                                        unsigned long long pledgedSrcSize);  /* pledgedSrcSize is optional and can be zero == unknown */

ZSTDMT144_API size_t ZSTDMT144_initCStream_usingCDict(ZSTDMT144_CCtx* mtctx,
                                        const ZSTD144_CDict* cdict,
                                        ZSTD144_frameParameters fparams,
                                        unsigned long long pledgedSrcSize);  /* note : zero means empty */

/* ZSTDMT144_parameter :
 * List of parameters that can be set using ZSTDMT144_setMTCtxParameter() */
typedef enum {
    ZSTDMT144_p_jobSize,     /* Each job is compressed in parallel. By default, this value is dynamically determined depending on compression parameters. Can be set explicitly here. */
    ZSTDMT144_p_overlapLog,  /* Each job may reload a part of previous job to enhance compression ratio; 0 == no overlap, 6(default) == use 1/8th of window, >=9 == use full window. This is a "sticky" parameter : its value will be re-used on next compression job */
    ZSTDMT144_p_rsyncable    /* Enables rsyncable mode. */
} ZSTDMT144_parameter;

/* ZSTDMT144_setMTCtxParameter() :
 * allow setting individual parameters, one at a time, among a list of enums defined in ZSTDMT144_parameter.
 * The function must be called typically after ZSTD144_createCCtx() but __before ZSTDMT144_init*() !__
 * Parameters not explicitly reset by ZSTDMT144_init*() remain the same in consecutive compression sessions.
 * @return : 0, or an error code (which can be tested using ZSTD144_isError()) */
ZSTDMT144_API size_t ZSTDMT144_setMTCtxParameter(ZSTDMT144_CCtx* mtctx, ZSTDMT144_parameter parameter, int value);

/* ZSTDMT144_getMTCtxParameter() :
 * Query the ZSTDMT144_CCtx for a parameter value.
 * @return : 0, or an error code (which can be tested using ZSTD144_isError()) */
ZSTDMT144_API size_t ZSTDMT144_getMTCtxParameter(ZSTDMT144_CCtx* mtctx, ZSTDMT144_parameter parameter, int* value);


/*! ZSTDMT144_compressStream_generic() :
 *  Combines ZSTDMT144_compressStream() with optional ZSTDMT144_flushStream() or ZSTDMT144_endStream()
 *  depending on flush directive.
 * @return : minimum amount of data still to be flushed
 *           0 if fully flushed
 *           or an error code
 *  note : needs to be init using any ZSTD144_initCStream*() variant */
ZSTDMT144_API size_t ZSTDMT144_compressStream_generic(ZSTDMT144_CCtx* mtctx,
                                                ZSTD144_outBuffer* output,
                                                ZSTD144_inBuffer* input,
                                                ZSTD144_EndDirective endOp);


/* ========================================================
 * ===  Private interface, for use by ZSTD144_compress.c   ===
 * ===  Not exposed in libzstd. Never invoke directly   ===
 * ======================================================== */

 /*! ZSTDMT144_toFlushNow()
  *  Tell how many bytes are ready to be flushed immediately.
  *  Probe the oldest active job (not yet entirely flushed) and check its output buffer.
  *  If return 0, it means there is no active job,
  *  or, it means oldest job is still active, but everything produced has been flushed so far,
  *  therefore flushing is limited by speed of oldest job. */
size_t ZSTDMT144_toFlushNow(ZSTDMT144_CCtx* mtctx);

/*! ZSTDMT144_CCtxParam_setMTCtxParameter()
 *  like ZSTDMT144_setMTCtxParameter(), but into a ZSTD144_CCtx_Params */
size_t ZSTDMT144_CCtxParam_setMTCtxParameter(ZSTD144_CCtx_params* params, ZSTDMT144_parameter parameter, int value);

/*! ZSTDMT144_CCtxParam_setNbWorkers()
 *  Set nbWorkers, and clamp it.
 *  Also reset jobSize and overlapLog */
size_t ZSTDMT144_CCtxParam_setNbWorkers(ZSTD144_CCtx_params* params, unsigned nbWorkers);

/*! ZSTDMT144_updateCParams_whileCompressing() :
 *  Updates only a selected set of compression parameters, to remain compatible with current frame.
 *  New parameters will be applied to next compression job. */
void ZSTDMT144_updateCParams_whileCompressing(ZSTDMT144_CCtx* mtctx, const ZSTD144_CCtx_params* cctxParams);

/*! ZSTDMT144_getFrameProgression():
 *  tells how much data has been consumed (input) and produced (output) for current frame.
 *  able to count progression inside worker threads.
 */
ZSTD144_frameProgression ZSTDMT144_getFrameProgression(ZSTDMT144_CCtx* mtctx);


/*! ZSTDMT144_initCStream_internal() :
 *  Private use only. Init streaming operation.
 *  expects params to be valid.
 *  must receive dict, or cdict, or none, but not both.
 *  @return : 0, or an error code */
size_t ZSTDMT144_initCStream_internal(ZSTDMT144_CCtx* zcs,
                    const void* dict, size_t dictSize, ZSTD144_dictContentType_e dictContentType,
                    const ZSTD144_CDict* cdict,
                    ZSTD144_CCtx_params params, unsigned long long pledgedSrcSize);


#if defined (__cplusplus)
}
#endif

#endif   /* ZSTDMT144_COMPRESS_H */
