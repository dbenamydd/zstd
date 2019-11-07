/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* Note : this module is expected to remain private, do not expose it */

#ifndef ERROR_H_MODULE
#define ERROR_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif


/* ****************************************
*  Dependencies
******************************************/
#include <stddef.h>        /* size_t */
#include "zstd_errors.h"  /* enum list */


/* ****************************************
*  Compiler-specific
******************************************/
#if defined(__GNUC__)
#  define ERR144_STATIC static __attribute__((unused))
#elif defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#  define ERR144_STATIC static inline
#elif defined(_MSC_VER)
#  define ERR144_STATIC static __inline
#else
#  define ERR144_STATIC static  /* this version may generate warnings for unused static functions; disable the relevant warning */
#endif


/*-****************************************
*  Customization (error_public.h)
******************************************/
typedef ZSTD144_ErrorCode ERR144_enum;
#define PREFIX(name) ZSTD144_error_##name


/*-****************************************
*  Error codes handling
******************************************/
#undef ERROR   /* reported already defined on VS 2015 (Rich Geldreich) */
#define ERROR(name) ZSTD144_ERROR(name)
#define ZSTD144_ERROR(name) ((size_t)-PREFIX(name))

ERR144_STATIC unsigned ERR144_isError(size_t code) { return (code > ERROR(maxCode)); }

ERR144_STATIC ERR144_enum ERR144_getErrorCode(size_t code) { if (!ERR144_isError(code)) return (ERR144_enum)0; return (ERR144_enum) (0-code); }


/*-****************************************
*  Error Strings
******************************************/

const char* ERR144_getErrorString(ERR144_enum code);   /* error_private.c */

ERR144_STATIC const char* ERR144_getErrorName(size_t code)
{
    return ERR144_getErrorString(ERR144_getErrorCode(code));
}

#if defined (__cplusplus)
}
#endif

#endif /* ERROR_H_MODULE */
