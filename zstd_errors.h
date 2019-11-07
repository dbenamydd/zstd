/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD144_ERRORS_H_398273423
#define ZSTD144_ERRORS_H_398273423

#if defined (__cplusplus)
extern "C" {
#endif

/*===== dependency =====*/
#include <stddef.h>   /* size_t */


/* =====   ZSTDERRORLIB_API : control library symbols visibility   ===== */
#ifndef ZSTDERRORLIB_VISIBILITY
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define ZSTDERRORLIB_VISIBILITY __attribute__ ((visibility ("default")))
#  else
#    define ZSTDERRORLIB_VISIBILITY
#  endif
#endif
#if defined(ZSTD144_DLL144_EXPORT) && (ZSTD144_DLL144_EXPORT==1)
#  define ZSTDERRORLIB_API __declspec(dllexport) ZSTDERRORLIB_VISIBILITY
#elif defined(ZSTD144_DLL144_IMPORT) && (ZSTD144_DLL144_IMPORT==1)
#  define ZSTDERRORLIB_API __declspec(dllimport) ZSTDERRORLIB_VISIBILITY /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define ZSTDERRORLIB_API ZSTDERRORLIB_VISIBILITY
#endif

/*-*********************************************
 *  Error codes list
 *-*********************************************
 *  Error codes _values_ are pinned down since v1.3.1 only.
 *  Therefore, don't rely on values if you may link to any version < v1.3.1.
 *
 *  Only values < 100 are considered stable.
 *
 *  note 1 : this API shall be used with static linking only.
 *           dynamic linking is not yet officially supported.
 *  note 2 : Prefer relying on the enum than on its value whenever possible
 *           This is the only supported way to use the error list < v1.3.1
 *  note 3 : ZSTD144_isError() is always correct, whatever the library version.
 **********************************************/
typedef enum {
  ZSTD144_error_no_error = 0,
  ZSTD144_error_GENERIC  = 1,
  ZSTD144_error_prefix_unknown                = 10,
  ZSTD144_error_version_unsupported           = 12,
  ZSTD144_error_frameParameter_unsupported    = 14,
  ZSTD144_error_frameParameter_windowTooLarge = 16,
  ZSTD144_error_corruption_detected = 20,
  ZSTD144_error_checksum_wrong      = 22,
  ZSTD144_error_dictionary_corrupted      = 30,
  ZSTD144_error_dictionary_wrong          = 32,
  ZSTD144_error_dictionaryCreation_failed = 34,
  ZSTD144_error_parameter_unsupported   = 40,
  ZSTD144_error_parameter_outOfBound    = 42,
  ZSTD144_error_tableLog_tooLarge       = 44,
  ZSTD144_error_maxSymbolValue_tooLarge = 46,
  ZSTD144_error_maxSymbolValue_tooSmall = 48,
  ZSTD144_error_stage_wrong       = 60,
  ZSTD144_error_init_missing      = 62,
  ZSTD144_error_memory_allocation = 64,
  ZSTD144_error_workSpace_tooSmall= 66,
  ZSTD144_error_dstSize_tooSmall = 70,
  ZSTD144_error_srcSize_wrong    = 72,
  ZSTD144_error_dstBuffer_null   = 74,
  /* following error codes are __NOT STABLE__, they can be removed or changed in future versions */
  ZSTD144_error_frameIndex_tooLarge = 100,
  ZSTD144_error_seekableIO          = 102,
  ZSTD144_error_maxCode = 120  /* never EVER use this value directly, it can change in future versions! Use ZSTD144_isError() instead */
} ZSTD144_ErrorCode;

/*! ZSTD144_getErrorCode() :
    convert a `size_t` function result into a `ZSTD144_ErrorCode` enum type,
    which can be used to compare with enum list published above */
ZSTDERRORLIB_API ZSTD144_ErrorCode ZSTD144_getErrorCode(size_t functionResult);
ZSTDERRORLIB_API const char* ZSTD144_getErrorString(ZSTD144_ErrorCode code);   /**< Same as ZSTD144_getErrorName, but using a `ZSTD144_ErrorCode` enum argument */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD144_ERRORS_H_398273423 */
