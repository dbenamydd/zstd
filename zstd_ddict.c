/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* zstd_ddict.c :
 * concentrates all logic that needs to know the internals of ZSTD144_DDict object */

/*-*******************************************************
*  Dependencies
*********************************************************/
#include <string.h>      /* memcpy, memmove, memset */
#include "cpu.h"         /* bmi2 */
#include "mem.h"         /* low level memory routines */
#define FSE144_STATIC_LINKING_ONLY
#include "fse.h"
#define HUF144_STATIC_LINKING_ONLY
#include "huf.h"
#include "zstd_decompress_internal.h"
#include "zstd_ddict.h"

#if defined(ZSTD144_LEGACY_SUPPORT) && (ZSTD144_LEGACY_SUPPORT>=1)
#  include "zstd_legacy.h"
#endif



/*-*******************************************************
*  Types
*********************************************************/
struct ZSTD144_DDict_s {
    void* dictBuffer;
    const void* dictContent;
    size_t dictSize;
    ZSTD144_entropyDTables_t entropy;
    U32 dictID;
    U32 entropyPresent;
    ZSTD144_customMem cMem;
};  /* typedef'd to ZSTD144_DDict within "zstd.h" */

const void* ZSTD144_DDict_dictContent(const ZSTD144_DDict* ddict)
{
    assert(ddict != NULL);
    return ddict->dictContent;
}

size_t ZSTD144_DDict_dictSize(const ZSTD144_DDict* ddict)
{
    assert(ddict != NULL);
    return ddict->dictSize;
}

void ZSTD144_copyDDictParameters(ZSTD144_DCtx* dctx, const ZSTD144_DDict* ddict)
{
    DEBUGLOG(4, "ZSTD144_copyDDictParameters");
    assert(dctx != NULL);
    assert(ddict != NULL);
    dctx->dictID = ddict->dictID;
    dctx->prefixStart = ddict->dictContent;
    dctx->virtualStart = ddict->dictContent;
    dctx->dictEnd = (const BYTE*)ddict->dictContent + ddict->dictSize;
    dctx->previousDstEnd = dctx->dictEnd;
    if (ddict->entropyPresent) {
        dctx->litEntropy = 1;
        dctx->fseEntropy = 1;
        dctx->LLTptr = ddict->entropy.LLTable;
        dctx->MLTptr = ddict->entropy.MLTable;
        dctx->OFTptr = ddict->entropy.OFTable;
        dctx->HUFptr = ddict->entropy.hufTable;
        dctx->entropy.rep[0] = ddict->entropy.rep[0];
        dctx->entropy.rep[1] = ddict->entropy.rep[1];
        dctx->entropy.rep[2] = ddict->entropy.rep[2];
    } else {
        dctx->litEntropy = 0;
        dctx->fseEntropy = 0;
    }
}


static size_t
ZSTD144_loadEntropy_intoDDict(ZSTD144_DDict* ddict,
                           ZSTD144_dictContentType_e dictContentType)
{
    ddict->dictID = 0;
    ddict->entropyPresent = 0;
    if (dictContentType == ZSTD144_dct_rawContent) return 0;

    if (ddict->dictSize < 8) {
        if (dictContentType == ZSTD144_dct_fullDict)
            return ERROR(dictionary_corrupted);   /* only accept specified dictionaries */
        return 0;   /* pure content mode */
    }
    {   U32 const magic = MEM_readLE32(ddict->dictContent);
        if (magic != ZSTD144_MAGIC_DICTIONARY) {
            if (dictContentType == ZSTD144_dct_fullDict)
                return ERROR(dictionary_corrupted);   /* only accept specified dictionaries */
            return 0;   /* pure content mode */
        }
    }
    ddict->dictID = MEM_readLE32((const char*)ddict->dictContent + ZSTD144_FRAMEIDSIZE);

    /* load entropy tables */
    RETURN_ERROR_IF(ZSTD144_isError(ZSTD144_loadDEntropy(
            &ddict->entropy, ddict->dictContent, ddict->dictSize)),
        dictionary_corrupted);
    ddict->entropyPresent = 1;
    return 0;
}


static size_t ZSTD144_initDDict_internal(ZSTD144_DDict* ddict,
                                      const void* dict, size_t dictSize,
                                      ZSTD144_dictLoadMethod_e dictLoadMethod,
                                      ZSTD144_dictContentType_e dictContentType)
{
    if ((dictLoadMethod == ZSTD144_dlm_byRef) || (!dict) || (!dictSize)) {
        ddict->dictBuffer = NULL;
        ddict->dictContent = dict;
        if (!dict) dictSize = 0;
    } else {
        void* const internalBuffer = ZSTD144_malloc(dictSize, ddict->cMem);
        ddict->dictBuffer = internalBuffer;
        ddict->dictContent = internalBuffer;
        if (!internalBuffer) return ERROR(memory_allocation);
        memcpy(internalBuffer, dict, dictSize);
    }
    ddict->dictSize = dictSize;
    ddict->entropy.hufTable[0] = (HUF144_DTable)((HufLog)*0x1000001);  /* cover both little and big endian */

    /* parse dictionary content */
    FORWARD_IF_ERROR( ZSTD144_loadEntropy_intoDDict(ddict, dictContentType) );

    return 0;
}

ZSTD144_DDict* ZSTD144_createDDict_advanced(const void* dict, size_t dictSize,
                                      ZSTD144_dictLoadMethod_e dictLoadMethod,
                                      ZSTD144_dictContentType_e dictContentType,
                                      ZSTD144_customMem customMem)
{
    if (!customMem.customAlloc ^ !customMem.customFree) return NULL;

    {   ZSTD144_DDict* const ddict = (ZSTD144_DDict*) ZSTD144_malloc(sizeof(ZSTD144_DDict), customMem);
        if (ddict == NULL) return NULL;
        ddict->cMem = customMem;
        {   size_t const initResult = ZSTD144_initDDict_internal(ddict,
                                            dict, dictSize,
                                            dictLoadMethod, dictContentType);
            if (ZSTD144_isError(initResult)) {
                ZSTD144_freeDDict(ddict);
                return NULL;
        }   }
        return ddict;
    }
}

/*! ZSTD144_createDDict() :
*   Create a digested dictionary, to start decompression without startup delay.
*   `dict` content is copied inside DDict.
*   Consequently, `dict` can be released after `ZSTD144_DDict` creation */
ZSTD144_DDict* ZSTD144_createDDict(const void* dict, size_t dictSize)
{
    ZSTD144_customMem const allocator = { NULL, NULL, NULL };
    return ZSTD144_createDDict_advanced(dict, dictSize, ZSTD144_dlm_byCopy, ZSTD144_dct_auto, allocator);
}

/*! ZSTD144_createDDict_byReference() :
 *  Create a digested dictionary, to start decompression without startup delay.
 *  Dictionary content is simply referenced, it will be accessed during decompression.
 *  Warning : dictBuffer must outlive DDict (DDict must be freed before dictBuffer) */
ZSTD144_DDict* ZSTD144_createDDict_byReference(const void* dictBuffer, size_t dictSize)
{
    ZSTD144_customMem const allocator = { NULL, NULL, NULL };
    return ZSTD144_createDDict_advanced(dictBuffer, dictSize, ZSTD144_dlm_byRef, ZSTD144_dct_auto, allocator);
}


const ZSTD144_DDict* ZSTD144_initStaticDDict(
                                void* sBuffer, size_t sBufferSize,
                                const void* dict, size_t dictSize,
                                ZSTD144_dictLoadMethod_e dictLoadMethod,
                                ZSTD144_dictContentType_e dictContentType)
{
    size_t const neededSpace = sizeof(ZSTD144_DDict)
                             + (dictLoadMethod == ZSTD144_dlm_byRef ? 0 : dictSize);
    ZSTD144_DDict* const ddict = (ZSTD144_DDict*)sBuffer;
    assert(sBuffer != NULL);
    assert(dict != NULL);
    if ((size_t)sBuffer & 7) return NULL;   /* 8-aligned */
    if (sBufferSize < neededSpace) return NULL;
    if (dictLoadMethod == ZSTD144_dlm_byCopy) {
        memcpy(ddict+1, dict, dictSize);  /* local copy */
        dict = ddict+1;
    }
    if (ZSTD144_isError( ZSTD144_initDDict_internal(ddict,
                                              dict, dictSize,
                                              ZSTD144_dlm_byRef, dictContentType) ))
        return NULL;
    return ddict;
}


size_t ZSTD144_freeDDict(ZSTD144_DDict* ddict)
{
    if (ddict==NULL) return 0;   /* support free on NULL */
    {   ZSTD144_customMem const cMem = ddict->cMem;
        ZSTD144_free(ddict->dictBuffer, cMem);
        ZSTD144_free(ddict, cMem);
        return 0;
    }
}

/*! ZSTD144_estimateDDictSize() :
 *  Estimate amount of memory that will be needed to create a dictionary for decompression.
 *  Note : dictionary created by reference using ZSTD144_dlm_byRef are smaller */
size_t ZSTD144_estimateDDictSize(size_t dictSize, ZSTD144_dictLoadMethod_e dictLoadMethod)
{
    return sizeof(ZSTD144_DDict) + (dictLoadMethod == ZSTD144_dlm_byRef ? 0 : dictSize);
}

size_t ZSTD144_sizeof_DDict(const ZSTD144_DDict* ddict)
{
    if (ddict==NULL) return 0;   /* support sizeof on NULL */
    return sizeof(*ddict) + (ddict->dictBuffer ? ddict->dictSize : 0) ;
}

/*! ZSTD144_getDictID_fromDDict() :
 *  Provides the dictID of the dictionary loaded into `ddict`.
 *  If @return == 0, the dictionary is not conformant to Zstandard specification, or empty.
 *  Non-conformant dictionaries can still be loaded, but as content-only dictionaries. */
unsigned ZSTD144_getDictID_fromDDict(const ZSTD144_DDict* ddict)
{
    if (ddict==NULL) return 0;
    return ZSTD144_getDictID_fromDict(ddict->dictContent, ddict->dictSize);
}
