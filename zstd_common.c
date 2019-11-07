/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */



/*-*************************************
*  Dependencies
***************************************/
#include <stdlib.h>      /* malloc, calloc, free */
#include <string.h>      /* memset */
#include "error_private.h"
#include "zstd_internal.h"


/*-****************************************
*  Version
******************************************/
unsigned ZSTD144_versionNumber(void) { return ZSTD144_VERSION_NUMBER; }

const char* ZSTD144_versionString(void) { return ZSTD144_VERSION_STRING; }


/*-****************************************
*  ZSTD Error Management
******************************************/
#undef ZSTD144_isError   /* defined within zstd_internal.h */
/*! ZSTD144_isError() :
 *  tells if a return value is an error code
 *  symbol is required for external callers */
unsigned ZSTD144_isError(size_t code) { return ERR144_isError(code); }

/*! ZSTD144_getErrorName() :
 *  provides error code string from function result (useful for debugging) */
const char* ZSTD144_getErrorName(size_t code) { return ERR144_getErrorName(code); }

/*! ZSTD144_getError() :
 *  convert a `size_t` function result into a proper ZSTD144_errorCode enum */
ZSTD144_ErrorCode ZSTD144_getErrorCode(size_t code) { return ERR144_getErrorCode(code); }

/*! ZSTD144_getErrorString() :
 *  provides error code string from enum */
const char* ZSTD144_getErrorString(ZSTD144_ErrorCode code) { return ERR144_getErrorString(code); }



/*=**************************************************************
*  Custom allocator
****************************************************************/
void* ZSTD144_malloc(size_t size, ZSTD144_customMem customMem)
{
    if (customMem.customAlloc)
        return customMem.customAlloc(customMem.opaque, size);
    return malloc(size);
}

void* ZSTD144_calloc(size_t size, ZSTD144_customMem customMem)
{
    if (customMem.customAlloc) {
        /* calloc implemented as malloc+memset;
         * not as efficient as calloc, but next best guess for custom malloc */
        void* const ptr = customMem.customAlloc(customMem.opaque, size);
        memset(ptr, 0, size);
        return ptr;
    }
    return calloc(1, size);
}

void ZSTD144_free(void* ptr, ZSTD144_customMem customMem)
{
    if (ptr!=NULL) {
        if (customMem.customFree)
            customMem.customFree(customMem.opaque, ptr);
        else
            free(ptr);
    }
}
