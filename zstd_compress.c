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
#include <limits.h>         /* INT_MAX */
#include <string.h>         /* memset */
#include "cpu.h"
#include "mem.h"
#include "hist.h"           /* HIST144_countFast_wksp */
#define FSE144_STATIC_LINKING_ONLY   /* FSE144_encodeSymbol */
#include "fse.h"
#define HUF144_STATIC_LINKING_ONLY
#include "huf.h"
#include "zstd_compress_internal.h"
#include "zstd_compress_sequences.h"
#include "zstd_compress_literals.h"
#include "zstd_fast.h"
#include "zstd_double_fast.h"
#include "zstd_lazy.h"
#include "zstd_opt.h"
#include "zstd_ldm.h"


/*-*************************************
*  Helper functions
***************************************/
size_t ZSTD144_compressBound(size_t srcSize) {
    return ZSTD144_COMPRESSBOUND(srcSize);
}


/*-*************************************
*  Context memory management
***************************************/
struct ZSTD144_CDict_s {
    const void* dictContent;
    size_t dictContentSize;
    U32* entropyWorkspace; /* entropy workspace of HUF144_WORKSPACE_SIZE bytes */
    ZSTD144_cwksp workspace;
    ZSTD144_matchState_t matchState;
    ZSTD144_compressedBlockState_t cBlockState;
    ZSTD144_customMem customMem;
    U32 dictID;
    int compressionLevel; /* 0 indicates that advanced API was used to select CDict params */
};  /* typedef'd to ZSTD144_CDict within "zstd.h" */

ZSTD144_CCtx* ZSTD144_createCCtx(void)
{
    return ZSTD144_createCCtx_advanced(ZSTD144_defaultCMem);
}

static void ZSTD144_initCCtx(ZSTD144_CCtx* cctx, ZSTD144_customMem memManager)
{
    assert(cctx != NULL);
    memset(cctx, 0, sizeof(*cctx));
    cctx->customMem = memManager;
    cctx->bmi2 = ZSTD144_cpuid_bmi2(ZSTD144_cpuid());
    {   size_t const err = ZSTD144_CCtx_reset(cctx, ZSTD144_reset_parameters);
        assert(!ZSTD144_isError(err));
        (void)err;
    }
}

ZSTD144_CCtx* ZSTD144_createCCtx_advanced(ZSTD144_customMem customMem)
{
    ZSTD144_STATIC_ASSERT(zcss_init==0);
    ZSTD144_STATIC_ASSERT(ZSTD144_CONTENTSIZE_UNKNOWN==(0ULL - 1));
    if (!customMem.customAlloc ^ !customMem.customFree) return NULL;
    {   ZSTD144_CCtx* const cctx = (ZSTD144_CCtx*)ZSTD144_malloc(sizeof(ZSTD144_CCtx), customMem);
        if (!cctx) return NULL;
        ZSTD144_initCCtx(cctx, customMem);
        return cctx;
    }
}

ZSTD144_CCtx* ZSTD144_initStaticCCtx(void *workspace, size_t workspaceSize)
{
    ZSTD144_cwksp ws;
    ZSTD144_CCtx* cctx;
    if (workspaceSize <= sizeof(ZSTD144_CCtx)) return NULL;  /* minimum size */
    if ((size_t)workspace & 7) return NULL;  /* must be 8-aligned */
    ZSTD144_cwksp_init(&ws, workspace, workspaceSize);

    cctx = (ZSTD144_CCtx*)ZSTD144_cwksp_reserve_object(&ws, sizeof(ZSTD144_CCtx));
    if (cctx == NULL) {
        return NULL;
    }
    memset(cctx, 0, sizeof(ZSTD144_CCtx));
    ZSTD144_cwksp_move(&cctx->workspace, &ws);
    cctx->staticSize = workspaceSize;

    /* statically sized space. entropyWorkspace never moves (but prev/next block swap places) */
    if (!ZSTD144_cwksp_check_available(&cctx->workspace, HUF144_WORKSPACE_SIZE + 2 * sizeof(ZSTD144_compressedBlockState_t))) return NULL;
    cctx->blockState.prevCBlock = (ZSTD144_compressedBlockState_t*)ZSTD144_cwksp_reserve_object(&cctx->workspace, sizeof(ZSTD144_compressedBlockState_t));
    cctx->blockState.nextCBlock = (ZSTD144_compressedBlockState_t*)ZSTD144_cwksp_reserve_object(&cctx->workspace, sizeof(ZSTD144_compressedBlockState_t));
    cctx->entropyWorkspace = (U32*)ZSTD144_cwksp_reserve_object(
        &cctx->workspace, HUF144_WORKSPACE_SIZE);
    cctx->bmi2 = ZSTD144_cpuid_bmi2(ZSTD144_cpuid());
    return cctx;
}

/**
 * Clears and frees all of the dictionaries in the CCtx.
 */
static void ZSTD144_clearAllDicts(ZSTD144_CCtx* cctx)
{
    ZSTD144_free(cctx->localDict.dictBuffer, cctx->customMem);
    ZSTD144_freeCDict(cctx->localDict.cdict);
    memset(&cctx->localDict, 0, sizeof(cctx->localDict));
    memset(&cctx->prefixDict, 0, sizeof(cctx->prefixDict));
    cctx->cdict = NULL;
}

static size_t ZSTD144_sizeof_localDict(ZSTD144_localDict dict)
{
    size_t const bufferSize = dict.dictBuffer != NULL ? dict.dictSize : 0;
    size_t const cdictSize = ZSTD144_sizeof_CDict(dict.cdict);
    return bufferSize + cdictSize;
}

static void ZSTD144_freeCCtxContent(ZSTD144_CCtx* cctx)
{
    assert(cctx != NULL);
    assert(cctx->staticSize == 0);
    ZSTD144_clearAllDicts(cctx);
#ifdef ZSTD144_MULTITHREAD
    ZSTDMT144_freeCCtx(cctx->mtctx); cctx->mtctx = NULL;
#endif
    ZSTD144_cwksp_free(&cctx->workspace, cctx->customMem);
}

size_t ZSTD144_freeCCtx(ZSTD144_CCtx* cctx)
{
    if (cctx==NULL) return 0;   /* support free on NULL */
    RETURN_ERROR_IF(cctx->staticSize, memory_allocation,
                    "not compatible with static CCtx");
    {
        int cctxInWorkspace = ZSTD144_cwksp_owns_buffer(&cctx->workspace, cctx);
        ZSTD144_freeCCtxContent(cctx);
        if (!cctxInWorkspace) {
            ZSTD144_free(cctx, cctx->customMem);
        }
    }
    return 0;
}


static size_t ZSTD144_sizeof_mtctx(const ZSTD144_CCtx* cctx)
{
#ifdef ZSTD144_MULTITHREAD
    return ZSTDMT144_sizeof_CCtx(cctx->mtctx);
#else
    (void)cctx;
    return 0;
#endif
}


size_t ZSTD144_sizeof_CCtx(const ZSTD144_CCtx* cctx)
{
    if (cctx==NULL) return 0;   /* support sizeof on NULL */
    /* cctx may be in the workspace */
    return (cctx->workspace.workspace == cctx ? 0 : sizeof(*cctx))
           + ZSTD144_cwksp_sizeof(&cctx->workspace)
           + ZSTD144_sizeof_localDict(cctx->localDict)
           + ZSTD144_sizeof_mtctx(cctx);
}

size_t ZSTD144_sizeof_CStream(const ZSTD144_CStream* zcs)
{
    return ZSTD144_sizeof_CCtx(zcs);  /* same object */
}

/* private API call, for dictBuilder only */
const seqStore_t* ZSTD144_getSeqStore(const ZSTD144_CCtx* ctx) { return &(ctx->seqStore); }

static ZSTD144_CCtx_params ZSTD144_makeCCtxParamsFromCParams(
        ZSTD144_compressionParameters cParams)
{
    ZSTD144_CCtx_params cctxParams;
    memset(&cctxParams, 0, sizeof(cctxParams));
    cctxParams.cParams = cParams;
    cctxParams.compressionLevel = ZSTD144_CLEVEL_DEFAULT;  /* should not matter, as all cParams are presumed properly defined */
    assert(!ZSTD144_checkCParams(cParams));
    cctxParams.fParams.contentSizeFlag = 1;
    return cctxParams;
}

static ZSTD144_CCtx_params* ZSTD144_createCCtxParams_advanced(
        ZSTD144_customMem customMem)
{
    ZSTD144_CCtx_params* params;
    if (!customMem.customAlloc ^ !customMem.customFree) return NULL;
    params = (ZSTD144_CCtx_params*)ZSTD144_calloc(
            sizeof(ZSTD144_CCtx_params), customMem);
    if (!params) { return NULL; }
    params->customMem = customMem;
    params->compressionLevel = ZSTD144_CLEVEL_DEFAULT;
    params->fParams.contentSizeFlag = 1;
    return params;
}

ZSTD144_CCtx_params* ZSTD144_createCCtxParams(void)
{
    return ZSTD144_createCCtxParams_advanced(ZSTD144_defaultCMem);
}

size_t ZSTD144_freeCCtxParams(ZSTD144_CCtx_params* params)
{
    if (params == NULL) { return 0; }
    ZSTD144_free(params, params->customMem);
    return 0;
}

size_t ZSTD144_CCtxParams_reset(ZSTD144_CCtx_params* params)
{
    return ZSTD144_CCtxParams_init(params, ZSTD144_CLEVEL_DEFAULT);
}

size_t ZSTD144_CCtxParams_init(ZSTD144_CCtx_params* cctxParams, int compressionLevel) {
    RETURN_ERROR_IF(!cctxParams, GENERIC);
    memset(cctxParams, 0, sizeof(*cctxParams));
    cctxParams->compressionLevel = compressionLevel;
    cctxParams->fParams.contentSizeFlag = 1;
    return 0;
}

size_t ZSTD144_CCtxParams_init_advanced(ZSTD144_CCtx_params* cctxParams, ZSTD144_parameters params)
{
    RETURN_ERROR_IF(!cctxParams, GENERIC);
    FORWARD_IF_ERROR( ZSTD144_checkCParams(params.cParams) );
    memset(cctxParams, 0, sizeof(*cctxParams));
    assert(!ZSTD144_checkCParams(params.cParams));
    cctxParams->cParams = params.cParams;
    cctxParams->fParams = params.fParams;
    cctxParams->compressionLevel = ZSTD144_CLEVEL_DEFAULT;   /* should not matter, as all cParams are presumed properly defined */
    return 0;
}

/* ZSTD144_assignParamsToCCtxParams() :
 * params is presumed valid at this stage */
static ZSTD144_CCtx_params ZSTD144_assignParamsToCCtxParams(
        const ZSTD144_CCtx_params* cctxParams, ZSTD144_parameters params)
{
    ZSTD144_CCtx_params ret = *cctxParams;
    assert(!ZSTD144_checkCParams(params.cParams));
    ret.cParams = params.cParams;
    ret.fParams = params.fParams;
    ret.compressionLevel = ZSTD144_CLEVEL_DEFAULT;   /* should not matter, as all cParams are presumed properly defined */
    return ret;
}

ZSTD144_bounds ZSTD144_cParam_getBounds(ZSTD144_cParameter param)
{
    ZSTD144_bounds bounds = { 0, 0, 0 };

    switch(param)
    {
    case ZSTD144_c_compressionLevel:
        bounds.lowerBound = ZSTD144_minCLevel();
        bounds.upperBound = ZSTD144_maxCLevel();
        return bounds;

    case ZSTD144_c_windowLog:
        bounds.lowerBound = ZSTD144_WINDOWLOG_MIN;
        bounds.upperBound = ZSTD144_WINDOWLOG_MAX;
        return bounds;

    case ZSTD144_c_hashLog:
        bounds.lowerBound = ZSTD144_HASHLOG_MIN;
        bounds.upperBound = ZSTD144_HASHLOG_MAX;
        return bounds;

    case ZSTD144_c_chainLog:
        bounds.lowerBound = ZSTD144_CHAINLOG_MIN;
        bounds.upperBound = ZSTD144_CHAINLOG_MAX;
        return bounds;

    case ZSTD144_c_searchLog:
        bounds.lowerBound = ZSTD144_SEARCHLOG_MIN;
        bounds.upperBound = ZSTD144_SEARCHLOG_MAX;
        return bounds;

    case ZSTD144_c_minMatch:
        bounds.lowerBound = ZSTD144_MINMATCH_MIN;
        bounds.upperBound = ZSTD144_MINMATCH_MAX;
        return bounds;

    case ZSTD144_c_targetLength:
        bounds.lowerBound = ZSTD144_TARGETLENGTH_MIN;
        bounds.upperBound = ZSTD144_TARGETLENGTH_MAX;
        return bounds;

    case ZSTD144_c_strategy:
        bounds.lowerBound = ZSTD144_STRATEGY_MIN;
        bounds.upperBound = ZSTD144_STRATEGY_MAX;
        return bounds;

    case ZSTD144_c_contentSizeFlag:
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_checksumFlag:
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_dictIDFlag:
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_nbWorkers:
        bounds.lowerBound = 0;
#ifdef ZSTD144_MULTITHREAD
        bounds.upperBound = ZSTDMT144_NBWORKERS_MAX;
#else
        bounds.upperBound = 0;
#endif
        return bounds;

    case ZSTD144_c_jobSize:
        bounds.lowerBound = 0;
#ifdef ZSTD144_MULTITHREAD
        bounds.upperBound = ZSTDMT144_JOBSIZE_MAX;
#else
        bounds.upperBound = 0;
#endif
        return bounds;

    case ZSTD144_c_overlapLog:
        bounds.lowerBound = ZSTD144_OVERLAPLOG_MIN;
        bounds.upperBound = ZSTD144_OVERLAPLOG_MAX;
        return bounds;

    case ZSTD144_c_enableLongDistanceMatching:
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_ldmHashLog:
        bounds.lowerBound = ZSTD144_LDM_HASHLOG_MIN;
        bounds.upperBound = ZSTD144_LDM_HASHLOG_MAX;
        return bounds;

    case ZSTD144_c_ldmMinMatch:
        bounds.lowerBound = ZSTD144_LDM_MINMATCH_MIN;
        bounds.upperBound = ZSTD144_LDM_MINMATCH_MAX;
        return bounds;

    case ZSTD144_c_ldmBucketSizeLog:
        bounds.lowerBound = ZSTD144_LDM_BUCKETSIZELOG_MIN;
        bounds.upperBound = ZSTD144_LDM_BUCKETSIZELOG_MAX;
        return bounds;

    case ZSTD144_c_ldmHashRateLog:
        bounds.lowerBound = ZSTD144_LDM_HASHRATELOG_MIN;
        bounds.upperBound = ZSTD144_LDM_HASHRATELOG_MAX;
        return bounds;

    /* experimental parameters */
    case ZSTD144_c_rsyncable:
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_forceMaxWindow :
        bounds.lowerBound = 0;
        bounds.upperBound = 1;
        return bounds;

    case ZSTD144_c_format:
        ZSTD144_STATIC_ASSERT(ZSTD144_f_zstd1 < ZSTD144_f_zstd1_magicless);
        bounds.lowerBound = ZSTD144_f_zstd1;
        bounds.upperBound = ZSTD144_f_zstd1_magicless;   /* note : how to ensure at compile time that this is the highest value enum ? */
        return bounds;

    case ZSTD144_c_forceAttachDict:
        ZSTD144_STATIC_ASSERT(ZSTD144_dictDefaultAttach < ZSTD144_dictForceCopy);
        bounds.lowerBound = ZSTD144_dictDefaultAttach;
        bounds.upperBound = ZSTD144_dictForceLoad;       /* note : how to ensure at compile time that this is the highest value enum ? */
        return bounds;

    case ZSTD144_c_literalCompressionMode:
        ZSTD144_STATIC_ASSERT(ZSTD144_lcm_auto < ZSTD144_lcm_huffman && ZSTD144_lcm_huffman < ZSTD144_lcm_uncompressed);
        bounds.lowerBound = ZSTD144_lcm_auto;
        bounds.upperBound = ZSTD144_lcm_uncompressed;
        return bounds;

    case ZSTD144_c_targetCBlockSize:
        bounds.lowerBound = ZSTD144_TARGETCBLOCKSIZE_MIN;
        bounds.upperBound = ZSTD144_TARGETCBLOCKSIZE_MAX;
        return bounds;

    case ZSTD144_c_srcSizeHint:
        bounds.lowerBound = ZSTD144_SRCSIZEHINT_MIN;
        bounds.upperBound = ZSTD144_SRCSIZEHINT_MAX;
        return bounds;

    default:
        {   ZSTD144_bounds const boundError = { ERROR(parameter_unsupported), 0, 0 };
            return boundError;
        }
    }
}

/* ZSTD144_cParam_clampBounds:
 * Clamps the value into the bounded range.
 */
static size_t ZSTD144_cParam_clampBounds(ZSTD144_cParameter cParam, int* value)
{
    ZSTD144_bounds const bounds = ZSTD144_cParam_getBounds(cParam);
    if (ZSTD144_isError(bounds.error)) return bounds.error;
    if (*value < bounds.lowerBound) *value = bounds.lowerBound;
    if (*value > bounds.upperBound) *value = bounds.upperBound;
    return 0;
}

#define BOUNDCHECK(cParam, val) { \
    RETURN_ERROR_IF(!ZSTD144_cParam_withinBounds(cParam,val), \
                    parameter_outOfBound); \
}


static int ZSTD144_isUpdateAuthorized(ZSTD144_cParameter param)
{
    switch(param)
    {
    case ZSTD144_c_compressionLevel:
    case ZSTD144_c_hashLog:
    case ZSTD144_c_chainLog:
    case ZSTD144_c_searchLog:
    case ZSTD144_c_minMatch:
    case ZSTD144_c_targetLength:
    case ZSTD144_c_strategy:
        return 1;

    case ZSTD144_c_format:
    case ZSTD144_c_windowLog:
    case ZSTD144_c_contentSizeFlag:
    case ZSTD144_c_checksumFlag:
    case ZSTD144_c_dictIDFlag:
    case ZSTD144_c_forceMaxWindow :
    case ZSTD144_c_nbWorkers:
    case ZSTD144_c_jobSize:
    case ZSTD144_c_overlapLog:
    case ZSTD144_c_rsyncable:
    case ZSTD144_c_enableLongDistanceMatching:
    case ZSTD144_c_ldmHashLog:
    case ZSTD144_c_ldmMinMatch:
    case ZSTD144_c_ldmBucketSizeLog:
    case ZSTD144_c_ldmHashRateLog:
    case ZSTD144_c_forceAttachDict:
    case ZSTD144_c_literalCompressionMode:
    case ZSTD144_c_targetCBlockSize:
    case ZSTD144_c_srcSizeHint:
    default:
        return 0;
    }
}

size_t ZSTD144_CCtx_setParameter(ZSTD144_CCtx* cctx, ZSTD144_cParameter param, int value)
{
    DEBUGLOG(4, "ZSTD144_CCtx_setParameter (%i, %i)", (int)param, value);
    if (cctx->streamStage != zcss_init) {
        if (ZSTD144_isUpdateAuthorized(param)) {
            cctx->cParamsChanged = 1;
        } else {
            RETURN_ERROR(stage_wrong);
    }   }

    switch(param)
    {
    case ZSTD144_c_nbWorkers:
        RETURN_ERROR_IF((value!=0) && cctx->staticSize, parameter_unsupported,
                        "MT not compatible with static alloc");
        break;

    case ZSTD144_c_compressionLevel:
    case ZSTD144_c_windowLog:
    case ZSTD144_c_hashLog:
    case ZSTD144_c_chainLog:
    case ZSTD144_c_searchLog:
    case ZSTD144_c_minMatch:
    case ZSTD144_c_targetLength:
    case ZSTD144_c_strategy:
    case ZSTD144_c_ldmHashRateLog:
    case ZSTD144_c_format:
    case ZSTD144_c_contentSizeFlag:
    case ZSTD144_c_checksumFlag:
    case ZSTD144_c_dictIDFlag:
    case ZSTD144_c_forceMaxWindow:
    case ZSTD144_c_forceAttachDict:
    case ZSTD144_c_literalCompressionMode:
    case ZSTD144_c_jobSize:
    case ZSTD144_c_overlapLog:
    case ZSTD144_c_rsyncable:
    case ZSTD144_c_enableLongDistanceMatching:
    case ZSTD144_c_ldmHashLog:
    case ZSTD144_c_ldmMinMatch:
    case ZSTD144_c_ldmBucketSizeLog:
    case ZSTD144_c_targetCBlockSize:
    case ZSTD144_c_srcSizeHint:
        break;

    default: RETURN_ERROR(parameter_unsupported);
    }
    return ZSTD144_CCtxParams_setParameter(&cctx->requestedParams, param, value);
}

size_t ZSTD144_CCtxParams_setParameter(ZSTD144_CCtx_params* CCtxParams,
                                    ZSTD144_cParameter param, int value)
{
    DEBUGLOG(4, "ZSTD144_CCtxParams_setParameter (%i, %i)", (int)param, value);
    switch(param)
    {
    case ZSTD144_c_format :
        BOUNDCHECK(ZSTD144_c_format, value);
        CCtxParams->format = (ZSTD144_format_e)value;
        return (size_t)CCtxParams->format;

    case ZSTD144_c_compressionLevel : {
        FORWARD_IF_ERROR(ZSTD144_cParam_clampBounds(param, &value));
        if (value) {  /* 0 : does not change current level */
            CCtxParams->compressionLevel = value;
        }
        if (CCtxParams->compressionLevel >= 0) return (size_t)CCtxParams->compressionLevel;
        return 0;  /* return type (size_t) cannot represent negative values */
    }

    case ZSTD144_c_windowLog :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_windowLog, value);
        CCtxParams->cParams.windowLog = (U32)value;
        return CCtxParams->cParams.windowLog;

    case ZSTD144_c_hashLog :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_hashLog, value);
        CCtxParams->cParams.hashLog = (U32)value;
        return CCtxParams->cParams.hashLog;

    case ZSTD144_c_chainLog :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_chainLog, value);
        CCtxParams->cParams.chainLog = (U32)value;
        return CCtxParams->cParams.chainLog;

    case ZSTD144_c_searchLog :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_searchLog, value);
        CCtxParams->cParams.searchLog = (U32)value;
        return (size_t)value;

    case ZSTD144_c_minMatch :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_minMatch, value);
        CCtxParams->cParams.minMatch = value;
        return CCtxParams->cParams.minMatch;

    case ZSTD144_c_targetLength :
        BOUNDCHECK(ZSTD144_c_targetLength, value);
        CCtxParams->cParams.targetLength = value;
        return CCtxParams->cParams.targetLength;

    case ZSTD144_c_strategy :
        if (value!=0)   /* 0 => use default */
            BOUNDCHECK(ZSTD144_c_strategy, value);
        CCtxParams->cParams.strategy = (ZSTD144_strategy)value;
        return (size_t)CCtxParams->cParams.strategy;

    case ZSTD144_c_contentSizeFlag :
        /* Content size written in frame header _when known_ (default:1) */
        DEBUGLOG(4, "set content size flag = %u", (value!=0));
        CCtxParams->fParams.contentSizeFlag = value != 0;
        return CCtxParams->fParams.contentSizeFlag;

    case ZSTD144_c_checksumFlag :
        /* A 32-bits content checksum will be calculated and written at end of frame (default:0) */
        CCtxParams->fParams.checksumFlag = value != 0;
        return CCtxParams->fParams.checksumFlag;

    case ZSTD144_c_dictIDFlag : /* When applicable, dictionary's dictID is provided in frame header (default:1) */
        DEBUGLOG(4, "set dictIDFlag = %u", (value!=0));
        CCtxParams->fParams.noDictIDFlag = !value;
        return !CCtxParams->fParams.noDictIDFlag;

    case ZSTD144_c_forceMaxWindow :
        CCtxParams->forceWindow = (value != 0);
        return CCtxParams->forceWindow;

    case ZSTD144_c_forceAttachDict : {
        const ZSTD144_dictAttachPref_e pref = (ZSTD144_dictAttachPref_e)value;
        BOUNDCHECK(ZSTD144_c_forceAttachDict, pref);
        CCtxParams->attachDictPref = pref;
        return CCtxParams->attachDictPref;
    }

    case ZSTD144_c_literalCompressionMode : {
        const ZSTD144_literalCompressionMode_e lcm = (ZSTD144_literalCompressionMode_e)value;
        BOUNDCHECK(ZSTD144_c_literalCompressionMode, lcm);
        CCtxParams->literalCompressionMode = lcm;
        return CCtxParams->literalCompressionMode;
    }

    case ZSTD144_c_nbWorkers :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR_IF(value!=0, parameter_unsupported, "not compiled with multithreading");
        return 0;
#else
        FORWARD_IF_ERROR(ZSTD144_cParam_clampBounds(param, &value));
        CCtxParams->nbWorkers = value;
        return CCtxParams->nbWorkers;
#endif

    case ZSTD144_c_jobSize :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR_IF(value!=0, parameter_unsupported, "not compiled with multithreading");
        return 0;
#else
        /* Adjust to the minimum non-default value. */
        if (value != 0 && value < ZSTDMT144_JOBSIZE_MIN)
            value = ZSTDMT144_JOBSIZE_MIN;
        FORWARD_IF_ERROR(ZSTD144_cParam_clampBounds(param, &value));
        assert(value >= 0);
        CCtxParams->jobSize = value;
        return CCtxParams->jobSize;
#endif

    case ZSTD144_c_overlapLog :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR_IF(value!=0, parameter_unsupported, "not compiled with multithreading");
        return 0;
#else
        FORWARD_IF_ERROR(ZSTD144_cParam_clampBounds(ZSTD144_c_overlapLog, &value));
        CCtxParams->overlapLog = value;
        return CCtxParams->overlapLog;
#endif

    case ZSTD144_c_rsyncable :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR_IF(value!=0, parameter_unsupported, "not compiled with multithreading");
        return 0;
#else
        FORWARD_IF_ERROR(ZSTD144_cParam_clampBounds(ZSTD144_c_overlapLog, &value));
        CCtxParams->rsyncable = value;
        return CCtxParams->rsyncable;
#endif

    case ZSTD144_c_enableLongDistanceMatching :
        CCtxParams->ldmParams.enableLdm = (value!=0);
        return CCtxParams->ldmParams.enableLdm;

    case ZSTD144_c_ldmHashLog :
        if (value!=0)   /* 0 ==> auto */
            BOUNDCHECK(ZSTD144_c_ldmHashLog, value);
        CCtxParams->ldmParams.hashLog = value;
        return CCtxParams->ldmParams.hashLog;

    case ZSTD144_c_ldmMinMatch :
        if (value!=0)   /* 0 ==> default */
            BOUNDCHECK(ZSTD144_c_ldmMinMatch, value);
        CCtxParams->ldmParams.minMatchLength = value;
        return CCtxParams->ldmParams.minMatchLength;

    case ZSTD144_c_ldmBucketSizeLog :
        if (value!=0)   /* 0 ==> default */
            BOUNDCHECK(ZSTD144_c_ldmBucketSizeLog, value);
        CCtxParams->ldmParams.bucketSizeLog = value;
        return CCtxParams->ldmParams.bucketSizeLog;

    case ZSTD144_c_ldmHashRateLog :
        RETURN_ERROR_IF(value > ZSTD144_WINDOWLOG_MAX - ZSTD144_HASHLOG_MIN,
                        parameter_outOfBound);
        CCtxParams->ldmParams.hashRateLog = value;
        return CCtxParams->ldmParams.hashRateLog;

    case ZSTD144_c_targetCBlockSize :
        if (value!=0)   /* 0 ==> default */
            BOUNDCHECK(ZSTD144_c_targetCBlockSize, value);
        CCtxParams->targetCBlockSize = value;
        return CCtxParams->targetCBlockSize;

    case ZSTD144_c_srcSizeHint :
        if (value!=0)    /* 0 ==> default */
            BOUNDCHECK(ZSTD144_c_srcSizeHint, value);
        CCtxParams->srcSizeHint = value;
        return CCtxParams->srcSizeHint;

    default: RETURN_ERROR(parameter_unsupported, "unknown parameter");
    }
}

size_t ZSTD144_CCtx_getParameter(ZSTD144_CCtx* cctx, ZSTD144_cParameter param, int* value)
{
    return ZSTD144_CCtxParams_getParameter(&cctx->requestedParams, param, value);
}

size_t ZSTD144_CCtxParams_getParameter(
        ZSTD144_CCtx_params* CCtxParams, ZSTD144_cParameter param, int* value)
{
    switch(param)
    {
    case ZSTD144_c_format :
        *value = CCtxParams->format;
        break;
    case ZSTD144_c_compressionLevel :
        *value = CCtxParams->compressionLevel;
        break;
    case ZSTD144_c_windowLog :
        *value = (int)CCtxParams->cParams.windowLog;
        break;
    case ZSTD144_c_hashLog :
        *value = (int)CCtxParams->cParams.hashLog;
        break;
    case ZSTD144_c_chainLog :
        *value = (int)CCtxParams->cParams.chainLog;
        break;
    case ZSTD144_c_searchLog :
        *value = CCtxParams->cParams.searchLog;
        break;
    case ZSTD144_c_minMatch :
        *value = CCtxParams->cParams.minMatch;
        break;
    case ZSTD144_c_targetLength :
        *value = CCtxParams->cParams.targetLength;
        break;
    case ZSTD144_c_strategy :
        *value = (unsigned)CCtxParams->cParams.strategy;
        break;
    case ZSTD144_c_contentSizeFlag :
        *value = CCtxParams->fParams.contentSizeFlag;
        break;
    case ZSTD144_c_checksumFlag :
        *value = CCtxParams->fParams.checksumFlag;
        break;
    case ZSTD144_c_dictIDFlag :
        *value = !CCtxParams->fParams.noDictIDFlag;
        break;
    case ZSTD144_c_forceMaxWindow :
        *value = CCtxParams->forceWindow;
        break;
    case ZSTD144_c_forceAttachDict :
        *value = CCtxParams->attachDictPref;
        break;
    case ZSTD144_c_literalCompressionMode :
        *value = CCtxParams->literalCompressionMode;
        break;
    case ZSTD144_c_nbWorkers :
#ifndef ZSTD144_MULTITHREAD
        assert(CCtxParams->nbWorkers == 0);
#endif
        *value = CCtxParams->nbWorkers;
        break;
    case ZSTD144_c_jobSize :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR(parameter_unsupported, "not compiled with multithreading");
#else
        assert(CCtxParams->jobSize <= INT_MAX);
        *value = (int)CCtxParams->jobSize;
        break;
#endif
    case ZSTD144_c_overlapLog :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR(parameter_unsupported, "not compiled with multithreading");
#else
        *value = CCtxParams->overlapLog;
        break;
#endif
    case ZSTD144_c_rsyncable :
#ifndef ZSTD144_MULTITHREAD
        RETURN_ERROR(parameter_unsupported, "not compiled with multithreading");
#else
        *value = CCtxParams->rsyncable;
        break;
#endif
    case ZSTD144_c_enableLongDistanceMatching :
        *value = CCtxParams->ldmParams.enableLdm;
        break;
    case ZSTD144_c_ldmHashLog :
        *value = CCtxParams->ldmParams.hashLog;
        break;
    case ZSTD144_c_ldmMinMatch :
        *value = CCtxParams->ldmParams.minMatchLength;
        break;
    case ZSTD144_c_ldmBucketSizeLog :
        *value = CCtxParams->ldmParams.bucketSizeLog;
        break;
    case ZSTD144_c_ldmHashRateLog :
        *value = CCtxParams->ldmParams.hashRateLog;
        break;
    case ZSTD144_c_targetCBlockSize :
        *value = (int)CCtxParams->targetCBlockSize;
        break;
    case ZSTD144_c_srcSizeHint :
        *value = (int)CCtxParams->srcSizeHint;
        break;
    default: RETURN_ERROR(parameter_unsupported, "unknown parameter");
    }
    return 0;
}

/** ZSTD144_CCtx_setParametersUsingCCtxParams() :
 *  just applies `params` into `cctx`
 *  no action is performed, parameters are merely stored.
 *  If ZSTDMT is enabled, parameters are pushed to cctx->mtctx.
 *    This is possible even if a compression is ongoing.
 *    In which case, new parameters will be applied on the fly, starting with next compression job.
 */
size_t ZSTD144_CCtx_setParametersUsingCCtxParams(
        ZSTD144_CCtx* cctx, const ZSTD144_CCtx_params* params)
{
    DEBUGLOG(4, "ZSTD144_CCtx_setParametersUsingCCtxParams");
    RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
    RETURN_ERROR_IF(cctx->cdict, stage_wrong);

    cctx->requestedParams = *params;
    return 0;
}

ZSTDLIB_API size_t ZSTD144_CCtx_setPledgedSrcSize(ZSTD144_CCtx* cctx, unsigned long long pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_CCtx_setPledgedSrcSize to %u bytes", (U32)pledgedSrcSize);
    RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
    cctx->pledgedSrcSizePlusOne = pledgedSrcSize+1;
    return 0;
}

/**
 * Initializes the local dict using the requested parameters.
 * NOTE: This does not use the pledged src size, because it may be used for more
 * than one compression.
 */
static size_t ZSTD144_initLocalDict(ZSTD144_CCtx* cctx)
{
    ZSTD144_localDict* const dl = &cctx->localDict;
    ZSTD144_compressionParameters const cParams = ZSTD144_getCParamsFromCCtxParams(
            &cctx->requestedParams, 0, dl->dictSize);
    if (dl->dict == NULL) {
        /* No local dictionary. */
        assert(dl->dictBuffer == NULL);
        assert(dl->cdict == NULL);
        assert(dl->dictSize == 0);
        return 0;
    }
    if (dl->cdict != NULL) {
        assert(cctx->cdict == dl->cdict);
        /* Local dictionary already initialized. */
        return 0;
    }
    assert(dl->dictSize > 0);
    assert(cctx->cdict == NULL);
    assert(cctx->prefixDict.dict == NULL);

    dl->cdict = ZSTD144_createCDict_advanced(
            dl->dict,
            dl->dictSize,
            ZSTD144_dlm_byRef,
            dl->dictContentType,
            cParams,
            cctx->customMem);
    RETURN_ERROR_IF(!dl->cdict, memory_allocation);
    cctx->cdict = dl->cdict;
    return 0;
}

size_t ZSTD144_CCtx_loadDictionary_advanced(
        ZSTD144_CCtx* cctx, const void* dict, size_t dictSize,
        ZSTD144_dictLoadMethod_e dictLoadMethod, ZSTD144_dictContentType_e dictContentType)
{
    RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
    RETURN_ERROR_IF(cctx->staticSize, memory_allocation,
                    "no malloc for static CCtx");
    DEBUGLOG(4, "ZSTD144_CCtx_loadDictionary_advanced (size: %u)", (U32)dictSize);
    ZSTD144_clearAllDicts(cctx);  /* in case one already exists */
    if (dict == NULL || dictSize == 0)  /* no dictionary mode */
        return 0;
    if (dictLoadMethod == ZSTD144_dlm_byRef) {
        cctx->localDict.dict = dict;
    } else {
        void* dictBuffer = ZSTD144_malloc(dictSize, cctx->customMem);
        RETURN_ERROR_IF(!dictBuffer, memory_allocation);
        memcpy(dictBuffer, dict, dictSize);
        cctx->localDict.dictBuffer = dictBuffer;
        cctx->localDict.dict = dictBuffer;
    }
    cctx->localDict.dictSize = dictSize;
    cctx->localDict.dictContentType = dictContentType;
    return 0;
}

ZSTDLIB_API size_t ZSTD144_CCtx_loadDictionary_byReference(
      ZSTD144_CCtx* cctx, const void* dict, size_t dictSize)
{
    return ZSTD144_CCtx_loadDictionary_advanced(
            cctx, dict, dictSize, ZSTD144_dlm_byRef, ZSTD144_dct_auto);
}

ZSTDLIB_API size_t ZSTD144_CCtx_loadDictionary(ZSTD144_CCtx* cctx, const void* dict, size_t dictSize)
{
    return ZSTD144_CCtx_loadDictionary_advanced(
            cctx, dict, dictSize, ZSTD144_dlm_byCopy, ZSTD144_dct_auto);
}


size_t ZSTD144_CCtx_refCDict(ZSTD144_CCtx* cctx, const ZSTD144_CDict* cdict)
{
    RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
    /* Free the existing local cdict (if any) to save memory. */
    ZSTD144_clearAllDicts(cctx);
    cctx->cdict = cdict;
    return 0;
}

size_t ZSTD144_CCtx_refPrefix(ZSTD144_CCtx* cctx, const void* prefix, size_t prefixSize)
{
    return ZSTD144_CCtx_refPrefix_advanced(cctx, prefix, prefixSize, ZSTD144_dct_rawContent);
}

size_t ZSTD144_CCtx_refPrefix_advanced(
        ZSTD144_CCtx* cctx, const void* prefix, size_t prefixSize, ZSTD144_dictContentType_e dictContentType)
{
    RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
    ZSTD144_clearAllDicts(cctx);
    cctx->prefixDict.dict = prefix;
    cctx->prefixDict.dictSize = prefixSize;
    cctx->prefixDict.dictContentType = dictContentType;
    return 0;
}

/*! ZSTD144_CCtx_reset() :
 *  Also dumps dictionary */
size_t ZSTD144_CCtx_reset(ZSTD144_CCtx* cctx, ZSTD144_ResetDirective reset)
{
    if ( (reset == ZSTD144_reset_session_only)
      || (reset == ZSTD144_reset_session_and_parameters) ) {
        cctx->streamStage = zcss_init;
        cctx->pledgedSrcSizePlusOne = 0;
    }
    if ( (reset == ZSTD144_reset_parameters)
      || (reset == ZSTD144_reset_session_and_parameters) ) {
        RETURN_ERROR_IF(cctx->streamStage != zcss_init, stage_wrong);
        ZSTD144_clearAllDicts(cctx);
        return ZSTD144_CCtxParams_reset(&cctx->requestedParams);
    }
    return 0;
}


/** ZSTD144_checkCParams() :
    control CParam values remain within authorized range.
    @return : 0, or an error code if one value is beyond authorized range */
size_t ZSTD144_checkCParams(ZSTD144_compressionParameters cParams)
{
    BOUNDCHECK(ZSTD144_c_windowLog, (int)cParams.windowLog);
    BOUNDCHECK(ZSTD144_c_chainLog,  (int)cParams.chainLog);
    BOUNDCHECK(ZSTD144_c_hashLog,   (int)cParams.hashLog);
    BOUNDCHECK(ZSTD144_c_searchLog, (int)cParams.searchLog);
    BOUNDCHECK(ZSTD144_c_minMatch,  (int)cParams.minMatch);
    BOUNDCHECK(ZSTD144_c_targetLength,(int)cParams.targetLength);
    BOUNDCHECK(ZSTD144_c_strategy,  cParams.strategy);
    return 0;
}

/** ZSTD144_clampCParams() :
 *  make CParam values within valid range.
 *  @return : valid CParams */
static ZSTD144_compressionParameters
ZSTD144_clampCParams(ZSTD144_compressionParameters cParams)
{
#   define CLAMP_TYPE(cParam, val, type) {                                \
        ZSTD144_bounds const bounds = ZSTD144_cParam_getBounds(cParam);         \
        if ((int)val<bounds.lowerBound) val=(type)bounds.lowerBound;      \
        else if ((int)val>bounds.upperBound) val=(type)bounds.upperBound; \
    }
#   define CLAMP(cParam, val) CLAMP_TYPE(cParam, val, unsigned)
    CLAMP(ZSTD144_c_windowLog, cParams.windowLog);
    CLAMP(ZSTD144_c_chainLog,  cParams.chainLog);
    CLAMP(ZSTD144_c_hashLog,   cParams.hashLog);
    CLAMP(ZSTD144_c_searchLog, cParams.searchLog);
    CLAMP(ZSTD144_c_minMatch,  cParams.minMatch);
    CLAMP(ZSTD144_c_targetLength,cParams.targetLength);
    CLAMP_TYPE(ZSTD144_c_strategy,cParams.strategy, ZSTD144_strategy);
    return cParams;
}

/** ZSTD144_cycleLog() :
 *  condition for correct operation : hashLog > 1 */
static U32 ZSTD144_cycleLog(U32 hashLog, ZSTD144_strategy strat)
{
    U32 const btScale = ((U32)strat >= (U32)ZSTD144_btlazy2);
    return hashLog - btScale;
}

/** ZSTD144_adjustCParams_internal() :
 *  optimize `cPar` for a specified input (`srcSize` and `dictSize`).
 *  mostly downsize to reduce memory consumption and initialization latency.
 * `srcSize` can be ZSTD144_CONTENTSIZE_UNKNOWN when not known.
 *  note : for the time being, `srcSize==0` means "unknown" too, for compatibility with older convention.
 *  condition : cPar is presumed validated (can be checked using ZSTD144_checkCParams()). */
static ZSTD144_compressionParameters
ZSTD144_adjustCParams_internal(ZSTD144_compressionParameters cPar,
                            unsigned long long srcSize,
                            size_t dictSize)
{
    static const U64 minSrcSize = 513; /* (1<<9) + 1 */
    static const U64 maxWindowResize = 1ULL << (ZSTD144_WINDOWLOG_MAX-1);
    assert(ZSTD144_checkCParams(cPar)==0);

    if (dictSize && (srcSize+1<2) /* ZSTD144_CONTENTSIZE_UNKNOWN and 0 mean "unknown" */ )
        srcSize = minSrcSize;  /* presumed small when there is a dictionary */
    else if (srcSize == 0)
        srcSize = ZSTD144_CONTENTSIZE_UNKNOWN;  /* 0 == unknown : presumed large */

    /* resize windowLog if input is small enough, to use less memory */
    if ( (srcSize < maxWindowResize)
      && (dictSize < maxWindowResize) )  {
        U32 const tSize = (U32)(srcSize + dictSize);
        static U32 const hashSizeMin = 1 << ZSTD144_HASHLOG_MIN;
        U32 const srcLog = (tSize < hashSizeMin) ? ZSTD144_HASHLOG_MIN :
                            ZSTD144_highbit32(tSize-1) + 1;
        if (cPar.windowLog > srcLog) cPar.windowLog = srcLog;
    }
    if (cPar.hashLog > cPar.windowLog+1) cPar.hashLog = cPar.windowLog+1;
    {   U32 const cycleLog = ZSTD144_cycleLog(cPar.chainLog, cPar.strategy);
        if (cycleLog > cPar.windowLog)
            cPar.chainLog -= (cycleLog - cPar.windowLog);
    }

    if (cPar.windowLog < ZSTD144_WINDOWLOG_ABSOLUTEMIN)
        cPar.windowLog = ZSTD144_WINDOWLOG_ABSOLUTEMIN;  /* minimum wlog required for valid frame header */

    return cPar;
}

ZSTD144_compressionParameters
ZSTD144_adjustCParams(ZSTD144_compressionParameters cPar,
                   unsigned long long srcSize,
                   size_t dictSize)
{
    cPar = ZSTD144_clampCParams(cPar);   /* resulting cPar is necessarily valid (all parameters within range) */
    return ZSTD144_adjustCParams_internal(cPar, srcSize, dictSize);
}

ZSTD144_compressionParameters ZSTD144_getCParamsFromCCtxParams(
        const ZSTD144_CCtx_params* CCtxParams, U64 srcSizeHint, size_t dictSize)
{
    ZSTD144_compressionParameters cParams;
    if (srcSizeHint == ZSTD144_CONTENTSIZE_UNKNOWN && CCtxParams->srcSizeHint > 0) {
      srcSizeHint = CCtxParams->srcSizeHint;
    }
    cParams = ZSTD144_getCParams(CCtxParams->compressionLevel, srcSizeHint, dictSize);
    if (CCtxParams->ldmParams.enableLdm) cParams.windowLog = ZSTD144_LDM_DEFAULT_WINDOW_LOG;
    if (CCtxParams->cParams.windowLog) cParams.windowLog = CCtxParams->cParams.windowLog;
    if (CCtxParams->cParams.hashLog) cParams.hashLog = CCtxParams->cParams.hashLog;
    if (CCtxParams->cParams.chainLog) cParams.chainLog = CCtxParams->cParams.chainLog;
    if (CCtxParams->cParams.searchLog) cParams.searchLog = CCtxParams->cParams.searchLog;
    if (CCtxParams->cParams.minMatch) cParams.minMatch = CCtxParams->cParams.minMatch;
    if (CCtxParams->cParams.targetLength) cParams.targetLength = CCtxParams->cParams.targetLength;
    if (CCtxParams->cParams.strategy) cParams.strategy = CCtxParams->cParams.strategy;
    assert(!ZSTD144_checkCParams(cParams));
    return ZSTD144_adjustCParams_internal(cParams, srcSizeHint, dictSize);
}

static size_t
ZSTD144_sizeof_matchState(const ZSTD144_compressionParameters* const cParams,
                       const U32 forCCtx)
{
    size_t const chainSize = (cParams->strategy == ZSTD144_fast) ? 0 : ((size_t)1 << cParams->chainLog);
    size_t const hSize = ((size_t)1) << cParams->hashLog;
    U32    const hashLog3 = (forCCtx && cParams->minMatch==3) ? MIN(ZSTD144_HASHLOG3_MAX, cParams->windowLog) : 0;
    size_t const h3Size = hashLog3 ? ((size_t)1) << hashLog3 : 0;
    /* We don't use ZSTD144_cwksp_alloc_size() here because the tables aren't
     * surrounded by redzones in ASAN. */
    size_t const tableSpace = chainSize * sizeof(U32)
                            + hSize * sizeof(U32)
                            + h3Size * sizeof(U32);
    size_t const optPotentialSpace =
        ZSTD144_cwksp_alloc_size((MaxML+1) * sizeof(U32))
      + ZSTD144_cwksp_alloc_size((MaxLL+1) * sizeof(U32))
      + ZSTD144_cwksp_alloc_size((MaxOff+1) * sizeof(U32))
      + ZSTD144_cwksp_alloc_size((1<<Litbits) * sizeof(U32))
      + ZSTD144_cwksp_alloc_size((ZSTD144_OPT_NUM+1) * sizeof(ZSTD144_match_t))
      + ZSTD144_cwksp_alloc_size((ZSTD144_OPT_NUM+1) * sizeof(ZSTD144_optimal_t));
    size_t const optSpace = (forCCtx && (cParams->strategy >= ZSTD144_btopt))
                                ? optPotentialSpace
                                : 0;
    DEBUGLOG(4, "chainSize: %u - hSize: %u - h3Size: %u",
                (U32)chainSize, (U32)hSize, (U32)h3Size);
    return tableSpace + optSpace;
}

size_t ZSTD144_estimateCCtxSize_usingCCtxParams(const ZSTD144_CCtx_params* params)
{
    RETURN_ERROR_IF(params->nbWorkers > 0, GENERIC, "Estimate CCtx size is supported for single-threaded compression only.");
    {   ZSTD144_compressionParameters const cParams =
                ZSTD144_getCParamsFromCCtxParams(params, 0, 0);
        size_t const blockSize = MIN(ZSTD144_BLOCKSIZE_MAX, (size_t)1 << cParams.windowLog);
        U32    const divider = (cParams.minMatch==3) ? 3 : 4;
        size_t const maxNbSeq = blockSize / divider;
        size_t const tokenSpace = ZSTD144_cwksp_alloc_size(WILDCOPY_OVERLENGTH + blockSize)
                                + ZSTD144_cwksp_alloc_size(maxNbSeq * sizeof(seqDef))
                                + 3 * ZSTD144_cwksp_alloc_size(maxNbSeq * sizeof(BYTE));
        size_t const entropySpace = ZSTD144_cwksp_alloc_size(HUF144_WORKSPACE_SIZE);
        size_t const blockStateSpace = 2 * ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_compressedBlockState_t));
        size_t const matchStateSize = ZSTD144_sizeof_matchState(&cParams, /* forCCtx */ 1);

        size_t const ldmSpace = ZSTD144_ldm_getTableSize(params->ldmParams);
        size_t const ldmSeqSpace = ZSTD144_cwksp_alloc_size(ZSTD144_ldm_getMaxNbSeq(params->ldmParams, blockSize) * sizeof(rawSeq));

        size_t const neededSpace = entropySpace + blockStateSpace + tokenSpace +
                                   matchStateSize + ldmSpace + ldmSeqSpace;
        size_t const cctxSpace = ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_CCtx));

        DEBUGLOG(5, "sizeof(ZSTD144_CCtx) : %u", (U32)cctxSpace);
        DEBUGLOG(5, "estimate workspace : %u", (U32)neededSpace);
        return cctxSpace + neededSpace;
    }
}

size_t ZSTD144_estimateCCtxSize_usingCParams(ZSTD144_compressionParameters cParams)
{
    ZSTD144_CCtx_params const params = ZSTD144_makeCCtxParamsFromCParams(cParams);
    return ZSTD144_estimateCCtxSize_usingCCtxParams(&params);
}

static size_t ZSTD144_estimateCCtxSize_internal(int compressionLevel)
{
    ZSTD144_compressionParameters const cParams = ZSTD144_getCParams(compressionLevel, 0, 0);
    return ZSTD144_estimateCCtxSize_usingCParams(cParams);
}

size_t ZSTD144_estimateCCtxSize(int compressionLevel)
{
    int level;
    size_t memBudget = 0;
    for (level=MIN(compressionLevel, 1); level<=compressionLevel; level++) {
        size_t const newMB = ZSTD144_estimateCCtxSize_internal(level);
        if (newMB > memBudget) memBudget = newMB;
    }
    return memBudget;
}

size_t ZSTD144_estimateCStreamSize_usingCCtxParams(const ZSTD144_CCtx_params* params)
{
    RETURN_ERROR_IF(params->nbWorkers > 0, GENERIC, "Estimate CCtx size is supported for single-threaded compression only.");
    {   ZSTD144_compressionParameters const cParams =
                ZSTD144_getCParamsFromCCtxParams(params, 0, 0);
        size_t const CCtxSize = ZSTD144_estimateCCtxSize_usingCCtxParams(params);
        size_t const blockSize = MIN(ZSTD144_BLOCKSIZE_MAX, (size_t)1 << cParams.windowLog);
        size_t const inBuffSize = ((size_t)1 << cParams.windowLog) + blockSize;
        size_t const outBuffSize = ZSTD144_compressBound(blockSize) + 1;
        size_t const streamingSize = ZSTD144_cwksp_alloc_size(inBuffSize)
                                   + ZSTD144_cwksp_alloc_size(outBuffSize);

        return CCtxSize + streamingSize;
    }
}

size_t ZSTD144_estimateCStreamSize_usingCParams(ZSTD144_compressionParameters cParams)
{
    ZSTD144_CCtx_params const params = ZSTD144_makeCCtxParamsFromCParams(cParams);
    return ZSTD144_estimateCStreamSize_usingCCtxParams(&params);
}

static size_t ZSTD144_estimateCStreamSize_internal(int compressionLevel)
{
    ZSTD144_compressionParameters const cParams = ZSTD144_getCParams(compressionLevel, 0, 0);
    return ZSTD144_estimateCStreamSize_usingCParams(cParams);
}

size_t ZSTD144_estimateCStreamSize(int compressionLevel)
{
    int level;
    size_t memBudget = 0;
    for (level=MIN(compressionLevel, 1); level<=compressionLevel; level++) {
        size_t const newMB = ZSTD144_estimateCStreamSize_internal(level);
        if (newMB > memBudget) memBudget = newMB;
    }
    return memBudget;
}

/* ZSTD144_getFrameProgression():
 * tells how much data has been consumed (input) and produced (output) for current frame.
 * able to count progression inside worker threads (non-blocking mode).
 */
ZSTD144_frameProgression ZSTD144_getFrameProgression(const ZSTD144_CCtx* cctx)
{
#ifdef ZSTD144_MULTITHREAD
    if (cctx->appliedParams.nbWorkers > 0) {
        return ZSTDMT144_getFrameProgression(cctx->mtctx);
    }
#endif
    {   ZSTD144_frameProgression fp;
        size_t const buffered = (cctx->inBuff == NULL) ? 0 :
                                cctx->inBuffPos - cctx->inToCompress;
        if (buffered) assert(cctx->inBuffPos >= cctx->inToCompress);
        assert(buffered <= ZSTD144_BLOCKSIZE_MAX);
        fp.ingested = cctx->consumedSrcSize + buffered;
        fp.consumed = cctx->consumedSrcSize;
        fp.produced = cctx->producedCSize;
        fp.flushed  = cctx->producedCSize;   /* simplified; some data might still be left within streaming output buffer */
        fp.currentJobID = 0;
        fp.nbActiveWorkers = 0;
        return fp;
}   }

/*! ZSTD144_toFlushNow()
 *  Only useful for multithreading scenarios currently (nbWorkers >= 1).
 */
size_t ZSTD144_toFlushNow(ZSTD144_CCtx* cctx)
{
#ifdef ZSTD144_MULTITHREAD
    if (cctx->appliedParams.nbWorkers > 0) {
        return ZSTDMT144_toFlushNow(cctx->mtctx);
    }
#endif
    (void)cctx;
    return 0;   /* over-simplification; could also check if context is currently running in streaming mode, and in which case, report how many bytes are left to be flushed within output buffer */
}

static void ZSTD144_assertEqualCParams(ZSTD144_compressionParameters cParams1,
                                    ZSTD144_compressionParameters cParams2)
{
    (void)cParams1;
    (void)cParams2;
    assert(cParams1.windowLog    == cParams2.windowLog);
    assert(cParams1.chainLog     == cParams2.chainLog);
    assert(cParams1.hashLog      == cParams2.hashLog);
    assert(cParams1.searchLog    == cParams2.searchLog);
    assert(cParams1.minMatch     == cParams2.minMatch);
    assert(cParams1.targetLength == cParams2.targetLength);
    assert(cParams1.strategy     == cParams2.strategy);
}

static void ZSTD144_reset_compressedBlockState(ZSTD144_compressedBlockState_t* bs)
{
    int i;
    for (i = 0; i < ZSTD144_REP_NUM; ++i)
        bs->rep[i] = repStartValue[i];
    bs->entropy.huf.repeatMode = HUF144_repeat_none;
    bs->entropy.fse.offcode_repeatMode = FSE144_repeat_none;
    bs->entropy.fse.matchlength_repeatMode = FSE144_repeat_none;
    bs->entropy.fse.litlength_repeatMode = FSE144_repeat_none;
}

/*! ZSTD144_invalidateMatchState()
 *  Invalidate all the matches in the match finder tables.
 *  Requires nextSrc and base to be set (can be NULL).
 */
static void ZSTD144_invalidateMatchState(ZSTD144_matchState_t* ms)
{
    ZSTD144_window_clear(&ms->window);

    ms->nextToUpdate = ms->window.dictLimit;
    ms->loadedDictEnd = 0;
    ms->opt.litLengthSum = 0;  /* force reset of btopt stats */
    ms->dictMatchState = NULL;
}

/**
 * Indicates whether this compression proceeds directly from user-provided
 * source buffer to user-provided destination buffer (ZSTDb_not_buffered), or
 * whether the context needs to buffer the input/output (ZSTDb_buffered).
 */
typedef enum {
    ZSTDb_not_buffered,
    ZSTDb_buffered
} ZSTD144_buffered_policy_e;

/**
 * Controls, for this matchState reset, whether the tables need to be cleared /
 * prepared for the coming compression (ZSTDcrp_makeClean), or whether the
 * tables can be left unclean (ZSTDcrp_leaveDirty), because we know that a
 * subsequent operation will overwrite the table space anyways (e.g., copying
 * the matchState contents in from a CDict).
 */
typedef enum {
    ZSTDcrp_makeClean,
    ZSTDcrp_leaveDirty
} ZSTD144_compResetPolicy_e;

/**
 * Controls, for this matchState reset, whether indexing can continue where it
 * left off (ZSTDirp_continue), or whether it needs to be restarted from zero
 * (ZSTDirp_reset).
 */
typedef enum {
    ZSTDirp_continue,
    ZSTDirp_reset
} ZSTD144_indexResetPolicy_e;

typedef enum {
    ZSTD144_resetTarget_CDict,
    ZSTD144_resetTarget_CCtx
} ZSTD144_resetTarget_e;

static size_t
ZSTD144_reset_matchState(ZSTD144_matchState_t* ms,
                      ZSTD144_cwksp* ws,
                const ZSTD144_compressionParameters* cParams,
                const ZSTD144_compResetPolicy_e crp,
                const ZSTD144_indexResetPolicy_e forceResetIndex,
                const ZSTD144_resetTarget_e forWho)
{
    size_t const chainSize = (cParams->strategy == ZSTD144_fast) ? 0 : ((size_t)1 << cParams->chainLog);
    size_t const hSize = ((size_t)1) << cParams->hashLog;
    U32    const hashLog3 = ((forWho == ZSTD144_resetTarget_CCtx) && cParams->minMatch==3) ? MIN(ZSTD144_HASHLOG3_MAX, cParams->windowLog) : 0;
    size_t const h3Size = hashLog3 ? ((size_t)1) << hashLog3 : 0;

    DEBUGLOG(4, "reset indices : %u", forceResetIndex == ZSTDirp_reset);
    if (forceResetIndex == ZSTDirp_reset) {
        memset(&ms->window, 0, sizeof(ms->window));
        ms->window.dictLimit = 1;    /* start from 1, so that 1st position is valid */
        ms->window.lowLimit = 1;     /* it ensures first and later CCtx usages compress the same */
        ms->window.nextSrc = ms->window.base + 1;   /* see issue #1241 */
        ZSTD144_cwksp_mark_tables_dirty(ws);
    }

    ms->hashLog3 = hashLog3;

    ZSTD144_invalidateMatchState(ms);

    assert(!ZSTD144_cwksp_reserve_failed(ws)); /* check that allocation hasn't already failed */

    ZSTD144_cwksp_clear_tables(ws);

    DEBUGLOG(5, "reserving table space");
    /* table Space */
    ms->hashTable = (U32*)ZSTD144_cwksp_reserve_table(ws, hSize * sizeof(U32));
    ms->chainTable = (U32*)ZSTD144_cwksp_reserve_table(ws, chainSize * sizeof(U32));
    ms->hashTable3 = (U32*)ZSTD144_cwksp_reserve_table(ws, h3Size * sizeof(U32));
    RETURN_ERROR_IF(ZSTD144_cwksp_reserve_failed(ws), memory_allocation,
                    "failed a workspace allocation in ZSTD144_reset_matchState");

    DEBUGLOG(4, "reset table : %u", crp!=ZSTDcrp_leaveDirty);
    if (crp!=ZSTDcrp_leaveDirty) {
        /* reset tables only */
        ZSTD144_cwksp_clean_tables(ws);
    }

    /* opt parser space */
    if ((forWho == ZSTD144_resetTarget_CCtx) && (cParams->strategy >= ZSTD144_btopt)) {
        DEBUGLOG(4, "reserving optimal parser space");
        ms->opt.litFreq = (unsigned*)ZSTD144_cwksp_reserve_aligned(ws, (1<<Litbits) * sizeof(unsigned));
        ms->opt.litLengthFreq = (unsigned*)ZSTD144_cwksp_reserve_aligned(ws, (MaxLL+1) * sizeof(unsigned));
        ms->opt.matchLengthFreq = (unsigned*)ZSTD144_cwksp_reserve_aligned(ws, (MaxML+1) * sizeof(unsigned));
        ms->opt.offCodeFreq = (unsigned*)ZSTD144_cwksp_reserve_aligned(ws, (MaxOff+1) * sizeof(unsigned));
        ms->opt.matchTable = (ZSTD144_match_t*)ZSTD144_cwksp_reserve_aligned(ws, (ZSTD144_OPT_NUM+1) * sizeof(ZSTD144_match_t));
        ms->opt.priceTable = (ZSTD144_optimal_t*)ZSTD144_cwksp_reserve_aligned(ws, (ZSTD144_OPT_NUM+1) * sizeof(ZSTD144_optimal_t));
    }

    ms->cParams = *cParams;

    RETURN_ERROR_IF(ZSTD144_cwksp_reserve_failed(ws), memory_allocation,
                    "failed a workspace allocation in ZSTD144_reset_matchState");

    return 0;
}

/* ZSTD144_indexTooCloseToMax() :
 * minor optimization : prefer memset() rather than reduceIndex()
 * which is measurably slow in some circumstances (reported for Visual Studio).
 * Works when re-using a context for a lot of smallish inputs :
 * if all inputs are smaller than ZSTD144_INDEXOVERFLOW_MARGIN,
 * memset() will be triggered before reduceIndex().
 */
#define ZSTD144_INDEXOVERFLOW_MARGIN (16 MB)
static int ZSTD144_indexTooCloseToMax(ZSTD144_window_t w)
{
    return (size_t)(w.nextSrc - w.base) > (ZSTD144_CURRENT_MAX - ZSTD144_INDEXOVERFLOW_MARGIN);
}

/*! ZSTD144_resetCCtx_internal() :
    note : `params` are assumed fully validated at this stage */
static size_t ZSTD144_resetCCtx_internal(ZSTD144_CCtx* zc,
                                      ZSTD144_CCtx_params params,
                                      U64 const pledgedSrcSize,
                                      ZSTD144_compResetPolicy_e const crp,
                                      ZSTD144_buffered_policy_e const zbuff)
{
    ZSTD144_cwksp* const ws = &zc->workspace;
    DEBUGLOG(4, "ZSTD144_resetCCtx_internal: pledgedSrcSize=%u, wlog=%u",
                (U32)pledgedSrcSize, params.cParams.windowLog);
    assert(!ZSTD144_isError(ZSTD144_checkCParams(params.cParams)));

    zc->isFirstBlock = 1;

    if (params.ldmParams.enableLdm) {
        /* Adjust long distance matching parameters */
        ZSTD144_ldm_adjustParameters(&params.ldmParams, &params.cParams);
        assert(params.ldmParams.hashLog >= params.ldmParams.bucketSizeLog);
        assert(params.ldmParams.hashRateLog < 32);
        zc->ldmState.hashPower = ZSTD144_rollingHash_primePower(params.ldmParams.minMatchLength);
    }

    {   size_t const windowSize = MAX(1, (size_t)MIN(((U64)1 << params.cParams.windowLog), pledgedSrcSize));
        size_t const blockSize = MIN(ZSTD144_BLOCKSIZE_MAX, windowSize);
        U32    const divider = (params.cParams.minMatch==3) ? 3 : 4;
        size_t const maxNbSeq = blockSize / divider;
        size_t const tokenSpace = ZSTD144_cwksp_alloc_size(WILDCOPY_OVERLENGTH + blockSize)
                                + ZSTD144_cwksp_alloc_size(maxNbSeq * sizeof(seqDef))
                                + 3 * ZSTD144_cwksp_alloc_size(maxNbSeq * sizeof(BYTE));
        size_t const buffOutSize = (zbuff==ZSTDb_buffered) ? ZSTD144_compressBound(blockSize)+1 : 0;
        size_t const buffInSize = (zbuff==ZSTDb_buffered) ? windowSize + blockSize : 0;
        size_t const matchStateSize = ZSTD144_sizeof_matchState(&params.cParams, /* forCCtx */ 1);
        size_t const maxNbLdmSeq = ZSTD144_ldm_getMaxNbSeq(params.ldmParams, blockSize);

        ZSTD144_indexResetPolicy_e needsIndexReset = ZSTDirp_continue;

        if (ZSTD144_indexTooCloseToMax(zc->blockState.matchState.window)) {
            needsIndexReset = ZSTDirp_reset;
        }

        ZSTD144_cwksp_bump_oversized_duration(ws, 0);

        /* Check if workspace is large enough, alloc a new one if needed */
        {   size_t const cctxSpace = zc->staticSize ? ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_CCtx)) : 0;
            size_t const entropySpace = ZSTD144_cwksp_alloc_size(HUF144_WORKSPACE_SIZE);
            size_t const blockStateSpace = 2 * ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_compressedBlockState_t));
            size_t const bufferSpace = ZSTD144_cwksp_alloc_size(buffInSize) + ZSTD144_cwksp_alloc_size(buffOutSize);
            size_t const ldmSpace = ZSTD144_ldm_getTableSize(params.ldmParams);
            size_t const ldmSeqSpace = ZSTD144_cwksp_alloc_size(maxNbLdmSeq * sizeof(rawSeq));

            size_t const neededSpace =
                cctxSpace +
                entropySpace +
                blockStateSpace +
                ldmSpace +
                ldmSeqSpace +
                matchStateSize +
                tokenSpace +
                bufferSpace;

            int const workspaceTooSmall = ZSTD144_cwksp_sizeof(ws) < neededSpace;
            int const workspaceWasteful = ZSTD144_cwksp_check_wasteful(ws, neededSpace);

            DEBUGLOG(4, "Need %zuKB workspace, including %zuKB for match state, and %zuKB for buffers",
                        neededSpace>>10, matchStateSize>>10, bufferSpace>>10);
            DEBUGLOG(4, "windowSize: %zu - blockSize: %zu", windowSize, blockSize);

            if (workspaceTooSmall || workspaceWasteful) {
                DEBUGLOG(4, "Resize workspaceSize from %zuKB to %zuKB",
                            ZSTD144_cwksp_sizeof(ws) >> 10,
                            neededSpace >> 10);

                RETURN_ERROR_IF(zc->staticSize, memory_allocation, "static cctx : no resize");

                needsIndexReset = ZSTDirp_reset;

                ZSTD144_cwksp_free(ws, zc->customMem);
                FORWARD_IF_ERROR(ZSTD144_cwksp_create(ws, neededSpace, zc->customMem));

                DEBUGLOG(5, "reserving object space");
                /* Statically sized space.
                 * entropyWorkspace never moves,
                 * though prev/next block swap places */
                assert(ZSTD144_cwksp_check_available(ws, 2 * sizeof(ZSTD144_compressedBlockState_t)));
                zc->blockState.prevCBlock = (ZSTD144_compressedBlockState_t*) ZSTD144_cwksp_reserve_object(ws, sizeof(ZSTD144_compressedBlockState_t));
                RETURN_ERROR_IF(zc->blockState.prevCBlock == NULL, memory_allocation, "couldn't allocate prevCBlock");
                zc->blockState.nextCBlock = (ZSTD144_compressedBlockState_t*) ZSTD144_cwksp_reserve_object(ws, sizeof(ZSTD144_compressedBlockState_t));
                RETURN_ERROR_IF(zc->blockState.nextCBlock == NULL, memory_allocation, "couldn't allocate nextCBlock");
                zc->entropyWorkspace = (U32*) ZSTD144_cwksp_reserve_object(ws, HUF144_WORKSPACE_SIZE);
                RETURN_ERROR_IF(zc->blockState.nextCBlock == NULL, memory_allocation, "couldn't allocate entropyWorkspace");
        }   }

        ZSTD144_cwksp_clear(ws);

        /* init params */
        zc->appliedParams = params;
        zc->blockState.matchState.cParams = params.cParams;
        zc->pledgedSrcSizePlusOne = pledgedSrcSize+1;
        zc->consumedSrcSize = 0;
        zc->producedCSize = 0;
        if (pledgedSrcSize == ZSTD144_CONTENTSIZE_UNKNOWN)
            zc->appliedParams.fParams.contentSizeFlag = 0;
        DEBUGLOG(4, "pledged content size : %u ; flag : %u",
            (unsigned)pledgedSrcSize, zc->appliedParams.fParams.contentSizeFlag);
        zc->blockSize = blockSize;

        XXH_3264_reset(&zc->xxhState, 0);
        zc->stage = ZSTDcs_init;
        zc->dictID = 0;

        ZSTD144_reset_compressedBlockState(zc->blockState.prevCBlock);

        /* ZSTD144_wildcopy() is used to copy into the literals buffer,
         * so we have to oversize the buffer by WILDCOPY_OVERLENGTH bytes.
         */
        zc->seqStore.litStart = ZSTD144_cwksp_reserve_buffer(ws, blockSize + WILDCOPY_OVERLENGTH);
        zc->seqStore.maxNbLit = blockSize;

        /* buffers */
        zc->inBuffSize = buffInSize;
        zc->inBuff = (char*)ZSTD144_cwksp_reserve_buffer(ws, buffInSize);
        zc->outBuffSize = buffOutSize;
        zc->outBuff = (char*)ZSTD144_cwksp_reserve_buffer(ws, buffOutSize);

        /* ldm bucketOffsets table */
        if (params.ldmParams.enableLdm) {
            /* TODO: avoid memset? */
            size_t const ldmBucketSize =
                  ((size_t)1) << (params.ldmParams.hashLog -
                                  params.ldmParams.bucketSizeLog);
            zc->ldmState.bucketOffsets = ZSTD144_cwksp_reserve_buffer(ws, ldmBucketSize);
            memset(zc->ldmState.bucketOffsets, 0, ldmBucketSize);
        }

        /* sequences storage */
        ZSTD144_referenceExternalSequences(zc, NULL, 0);
        zc->seqStore.maxNbSeq = maxNbSeq;
        zc->seqStore.llCode = ZSTD144_cwksp_reserve_buffer(ws, maxNbSeq * sizeof(BYTE));
        zc->seqStore.mlCode = ZSTD144_cwksp_reserve_buffer(ws, maxNbSeq * sizeof(BYTE));
        zc->seqStore.ofCode = ZSTD144_cwksp_reserve_buffer(ws, maxNbSeq * sizeof(BYTE));
        zc->seqStore.sequencesStart = (seqDef*)ZSTD144_cwksp_reserve_aligned(ws, maxNbSeq * sizeof(seqDef));

        FORWARD_IF_ERROR(ZSTD144_reset_matchState(
            &zc->blockState.matchState,
            ws,
            &params.cParams,
            crp,
            needsIndexReset,
            ZSTD144_resetTarget_CCtx));

        /* ldm hash table */
        if (params.ldmParams.enableLdm) {
            /* TODO: avoid memset? */
            size_t const ldmHSize = ((size_t)1) << params.ldmParams.hashLog;
            zc->ldmState.hashTable = (ldmEntry_t*)ZSTD144_cwksp_reserve_aligned(ws, ldmHSize * sizeof(ldmEntry_t));
            memset(zc->ldmState.hashTable, 0, ldmHSize * sizeof(ldmEntry_t));
            zc->ldmSequences = (rawSeq*)ZSTD144_cwksp_reserve_aligned(ws, maxNbLdmSeq * sizeof(rawSeq));
            zc->maxNbLdmSequences = maxNbLdmSeq;

            memset(&zc->ldmState.window, 0, sizeof(zc->ldmState.window));
            ZSTD144_window_clear(&zc->ldmState.window);
        }

        DEBUGLOG(3, "wksp: finished allocating, %zd bytes remain available", ZSTD144_cwksp_available_space(ws));

        return 0;
    }
}

/* ZSTD144_invalidateRepCodes() :
 * ensures next compression will not use repcodes from previous block.
 * Note : only works with regular variant;
 *        do not use with extDict variant ! */
void ZSTD144_invalidateRepCodes(ZSTD144_CCtx* cctx) {
    int i;
    for (i=0; i<ZSTD144_REP_NUM; i++) cctx->blockState.prevCBlock->rep[i] = 0;
    assert(!ZSTD144_window_hasExtDict(cctx->blockState.matchState.window));
}

/* These are the approximate sizes for each strategy past which copying the
 * dictionary tables into the working context is faster than using them
 * in-place.
 */
static const size_t attachDictSizeCutoffs[ZSTD144_STRATEGY_MAX+1] = {
    8 KB,  /* unused */
    8 KB,  /* ZSTD144_fast */
    16 KB, /* ZSTD144_dfast */
    32 KB, /* ZSTD144_greedy */
    32 KB, /* ZSTD144_lazy */
    32 KB, /* ZSTD144_lazy2 */
    32 KB, /* ZSTD144_btlazy2 */
    32 KB, /* ZSTD144_btopt */
    8 KB,  /* ZSTD144_btultra */
    8 KB   /* ZSTD144_btultra2 */
};

static int ZSTD144_shouldAttachDict(const ZSTD144_CDict* cdict,
                                 const ZSTD144_CCtx_params* params,
                                 U64 pledgedSrcSize)
{
    size_t cutoff = attachDictSizeCutoffs[cdict->matchState.cParams.strategy];
    return ( pledgedSrcSize <= cutoff
          || pledgedSrcSize == ZSTD144_CONTENTSIZE_UNKNOWN
          || params->attachDictPref == ZSTD144_dictForceAttach )
        && params->attachDictPref != ZSTD144_dictForceCopy
        && !params->forceWindow; /* dictMatchState isn't correctly
                                 * handled in _enforceMaxDist */
}

static size_t
ZSTD144_resetCCtx_byAttachingCDict(ZSTD144_CCtx* cctx,
                        const ZSTD144_CDict* cdict,
                        ZSTD144_CCtx_params params,
                        U64 pledgedSrcSize,
                        ZSTD144_buffered_policy_e zbuff)
{
    {   const ZSTD144_compressionParameters* const cdict_cParams = &cdict->matchState.cParams;
        unsigned const windowLog = params.cParams.windowLog;
        assert(windowLog != 0);
        /* Resize working context table params for input only, since the dict
         * has its own tables. */
        params.cParams = ZSTD144_adjustCParams_internal(*cdict_cParams, pledgedSrcSize, 0);
        params.cParams.windowLog = windowLog;
        FORWARD_IF_ERROR(ZSTD144_resetCCtx_internal(cctx, params, pledgedSrcSize,
                                                 ZSTDcrp_makeClean, zbuff));
        assert(cctx->appliedParams.cParams.strategy == cdict_cParams->strategy);
    }

    {   const U32 cdictEnd = (U32)( cdict->matchState.window.nextSrc
                                  - cdict->matchState.window.base);
        const U32 cdictLen = cdictEnd - cdict->matchState.window.dictLimit;
        if (cdictLen == 0) {
            /* don't even attach dictionaries with no contents */
            DEBUGLOG(4, "skipping attaching empty dictionary");
        } else {
            DEBUGLOG(4, "attaching dictionary into context");
            cctx->blockState.matchState.dictMatchState = &cdict->matchState;

            /* prep working match state so dict matches never have negative indices
             * when they are translated to the working context's index space. */
            if (cctx->blockState.matchState.window.dictLimit < cdictEnd) {
                cctx->blockState.matchState.window.nextSrc =
                    cctx->blockState.matchState.window.base + cdictEnd;
                ZSTD144_window_clear(&cctx->blockState.matchState.window);
            }
            /* loadedDictEnd is expressed within the referential of the active context */
            cctx->blockState.matchState.loadedDictEnd = cctx->blockState.matchState.window.dictLimit;
    }   }

    cctx->dictID = cdict->dictID;

    /* copy block state */
    memcpy(cctx->blockState.prevCBlock, &cdict->cBlockState, sizeof(cdict->cBlockState));

    return 0;
}

static size_t ZSTD144_resetCCtx_byCopyingCDict(ZSTD144_CCtx* cctx,
                            const ZSTD144_CDict* cdict,
                            ZSTD144_CCtx_params params,
                            U64 pledgedSrcSize,
                            ZSTD144_buffered_policy_e zbuff)
{
    const ZSTD144_compressionParameters *cdict_cParams = &cdict->matchState.cParams;

    DEBUGLOG(4, "copying dictionary into context");

    {   unsigned const windowLog = params.cParams.windowLog;
        assert(windowLog != 0);
        /* Copy only compression parameters related to tables. */
        params.cParams = *cdict_cParams;
        params.cParams.windowLog = windowLog;
        FORWARD_IF_ERROR(ZSTD144_resetCCtx_internal(cctx, params, pledgedSrcSize,
                                                 ZSTDcrp_leaveDirty, zbuff));
        assert(cctx->appliedParams.cParams.strategy == cdict_cParams->strategy);
        assert(cctx->appliedParams.cParams.hashLog == cdict_cParams->hashLog);
        assert(cctx->appliedParams.cParams.chainLog == cdict_cParams->chainLog);
    }

    ZSTD144_cwksp_mark_tables_dirty(&cctx->workspace);

    /* copy tables */
    {   size_t const chainSize = (cdict_cParams->strategy == ZSTD144_fast) ? 0 : ((size_t)1 << cdict_cParams->chainLog);
        size_t const hSize =  (size_t)1 << cdict_cParams->hashLog;

        memcpy(cctx->blockState.matchState.hashTable,
               cdict->matchState.hashTable,
               hSize * sizeof(U32));
        memcpy(cctx->blockState.matchState.chainTable,
               cdict->matchState.chainTable,
               chainSize * sizeof(U32));
    }

    /* Zero the hashTable3, since the cdict never fills it */
    {   int const h3log = cctx->blockState.matchState.hashLog3;
        size_t const h3Size = h3log ? ((size_t)1 << h3log) : 0;
        assert(cdict->matchState.hashLog3 == 0);
        memset(cctx->blockState.matchState.hashTable3, 0, h3Size * sizeof(U32));
    }

    ZSTD144_cwksp_mark_tables_clean(&cctx->workspace);

    /* copy dictionary offsets */
    {   ZSTD144_matchState_t const* srcMatchState = &cdict->matchState;
        ZSTD144_matchState_t* dstMatchState = &cctx->blockState.matchState;
        dstMatchState->window       = srcMatchState->window;
        dstMatchState->nextToUpdate = srcMatchState->nextToUpdate;
        dstMatchState->loadedDictEnd= srcMatchState->loadedDictEnd;
    }

    cctx->dictID = cdict->dictID;

    /* copy block state */
    memcpy(cctx->blockState.prevCBlock, &cdict->cBlockState, sizeof(cdict->cBlockState));

    return 0;
}

/* We have a choice between copying the dictionary context into the working
 * context, or referencing the dictionary context from the working context
 * in-place. We decide here which strategy to use. */
static size_t ZSTD144_resetCCtx_usingCDict(ZSTD144_CCtx* cctx,
                            const ZSTD144_CDict* cdict,
                            const ZSTD144_CCtx_params* params,
                            U64 pledgedSrcSize,
                            ZSTD144_buffered_policy_e zbuff)
{

    DEBUGLOG(4, "ZSTD144_resetCCtx_usingCDict (pledgedSrcSize=%u)",
                (unsigned)pledgedSrcSize);

    if (ZSTD144_shouldAttachDict(cdict, params, pledgedSrcSize)) {
        return ZSTD144_resetCCtx_byAttachingCDict(
            cctx, cdict, *params, pledgedSrcSize, zbuff);
    } else {
        return ZSTD144_resetCCtx_byCopyingCDict(
            cctx, cdict, *params, pledgedSrcSize, zbuff);
    }
}

/*! ZSTD144_copyCCtx_internal() :
 *  Duplicate an existing context `srcCCtx` into another one `dstCCtx`.
 *  Only works during stage ZSTDcs_init (i.e. after creation, but before first call to ZSTD144_compressContinue()).
 *  The "context", in this case, refers to the hash and chain tables,
 *  entropy tables, and dictionary references.
 * `windowLog` value is enforced if != 0, otherwise value is copied from srcCCtx.
 * @return : 0, or an error code */
static size_t ZSTD144_copyCCtx_internal(ZSTD144_CCtx* dstCCtx,
                            const ZSTD144_CCtx* srcCCtx,
                            ZSTD144_frameParameters fParams,
                            U64 pledgedSrcSize,
                            ZSTD144_buffered_policy_e zbuff)
{
    DEBUGLOG(5, "ZSTD144_copyCCtx_internal");
    RETURN_ERROR_IF(srcCCtx->stage!=ZSTDcs_init, stage_wrong);

    memcpy(&dstCCtx->customMem, &srcCCtx->customMem, sizeof(ZSTD144_customMem));
    {   ZSTD144_CCtx_params params = dstCCtx->requestedParams;
        /* Copy only compression parameters related to tables. */
        params.cParams = srcCCtx->appliedParams.cParams;
        params.fParams = fParams;
        ZSTD144_resetCCtx_internal(dstCCtx, params, pledgedSrcSize,
                                ZSTDcrp_leaveDirty, zbuff);
        assert(dstCCtx->appliedParams.cParams.windowLog == srcCCtx->appliedParams.cParams.windowLog);
        assert(dstCCtx->appliedParams.cParams.strategy == srcCCtx->appliedParams.cParams.strategy);
        assert(dstCCtx->appliedParams.cParams.hashLog == srcCCtx->appliedParams.cParams.hashLog);
        assert(dstCCtx->appliedParams.cParams.chainLog == srcCCtx->appliedParams.cParams.chainLog);
        assert(dstCCtx->blockState.matchState.hashLog3 == srcCCtx->blockState.matchState.hashLog3);
    }

    ZSTD144_cwksp_mark_tables_dirty(&dstCCtx->workspace);

    /* copy tables */
    {   size_t const chainSize = (srcCCtx->appliedParams.cParams.strategy == ZSTD144_fast) ? 0 : ((size_t)1 << srcCCtx->appliedParams.cParams.chainLog);
        size_t const hSize =  (size_t)1 << srcCCtx->appliedParams.cParams.hashLog;
        int const h3log = srcCCtx->blockState.matchState.hashLog3;
        size_t const h3Size = h3log ? ((size_t)1 << h3log) : 0;

        memcpy(dstCCtx->blockState.matchState.hashTable,
               srcCCtx->blockState.matchState.hashTable,
               hSize * sizeof(U32));
        memcpy(dstCCtx->blockState.matchState.chainTable,
               srcCCtx->blockState.matchState.chainTable,
               chainSize * sizeof(U32));
        memcpy(dstCCtx->blockState.matchState.hashTable3,
               srcCCtx->blockState.matchState.hashTable3,
               h3Size * sizeof(U32));
    }

    ZSTD144_cwksp_mark_tables_clean(&dstCCtx->workspace);

    /* copy dictionary offsets */
    {
        const ZSTD144_matchState_t* srcMatchState = &srcCCtx->blockState.matchState;
        ZSTD144_matchState_t* dstMatchState = &dstCCtx->blockState.matchState;
        dstMatchState->window       = srcMatchState->window;
        dstMatchState->nextToUpdate = srcMatchState->nextToUpdate;
        dstMatchState->loadedDictEnd= srcMatchState->loadedDictEnd;
    }
    dstCCtx->dictID = srcCCtx->dictID;

    /* copy block state */
    memcpy(dstCCtx->blockState.prevCBlock, srcCCtx->blockState.prevCBlock, sizeof(*srcCCtx->blockState.prevCBlock));

    return 0;
}

/*! ZSTD144_copyCCtx() :
 *  Duplicate an existing context `srcCCtx` into another one `dstCCtx`.
 *  Only works during stage ZSTDcs_init (i.e. after creation, but before first call to ZSTD144_compressContinue()).
 *  pledgedSrcSize==0 means "unknown".
*   @return : 0, or an error code */
size_t ZSTD144_copyCCtx(ZSTD144_CCtx* dstCCtx, const ZSTD144_CCtx* srcCCtx, unsigned long long pledgedSrcSize)
{
    ZSTD144_frameParameters fParams = { 1 /*content*/, 0 /*checksum*/, 0 /*noDictID*/ };
    ZSTD144_buffered_policy_e const zbuff = (ZSTD144_buffered_policy_e)(srcCCtx->inBuffSize>0);
    ZSTD144_STATIC_ASSERT((U32)ZSTDb_buffered==1);
    if (pledgedSrcSize==0) pledgedSrcSize = ZSTD144_CONTENTSIZE_UNKNOWN;
    fParams.contentSizeFlag = (pledgedSrcSize != ZSTD144_CONTENTSIZE_UNKNOWN);

    return ZSTD144_copyCCtx_internal(dstCCtx, srcCCtx,
                                fParams, pledgedSrcSize,
                                zbuff);
}


#define ZSTD144_ROWSIZE 16
/*! ZSTD144_reduceTable() :
 *  reduce table indexes by `reducerValue`, or squash to zero.
 *  PreserveMark preserves "unsorted mark" for btlazy2 strategy.
 *  It must be set to a clear 0/1 value, to remove branch during inlining.
 *  Presume table size is a multiple of ZSTD144_ROWSIZE
 *  to help auto-vectorization */
FORCE_INLINE_TEMPLATE void
ZSTD144_reduceTable_internal (U32* const table, U32 const size, U32 const reducerValue, int const preserveMark)
{
    int const nbRows = (int)size / ZSTD144_ROWSIZE;
    int cellNb = 0;
    int rowNb;
    assert((size & (ZSTD144_ROWSIZE-1)) == 0);  /* multiple of ZSTD144_ROWSIZE */
    assert(size < (1U<<31));   /* can be casted to int */

#if defined (MEMORY_SANITIZER) && !defined (ZSTD144_MSAN_DONT_POISON_WORKSPACE)
    /* To validate that the table re-use logic is sound, and that we don't
     * access table space that we haven't cleaned, we re-"poison" the table
     * space every time we mark it dirty.
     *
     * This function however is intended to operate on those dirty tables and
     * re-clean them. So when this function is used correctly, we can unpoison
     * the memory it operated on. This introduces a blind spot though, since
     * if we now try to operate on __actually__ poisoned memory, we will not
     * detect that. */
    __msan_unpoison(table, size * sizeof(U32));
#endif

    for (rowNb=0 ; rowNb < nbRows ; rowNb++) {
        int column;
        for (column=0; column<ZSTD144_ROWSIZE; column++) {
            if (preserveMark) {
                U32 const adder = (table[cellNb] == ZSTD144_DUBT_UNSORTED_MARK) ? reducerValue : 0;
                table[cellNb] += adder;
            }
            if (table[cellNb] < reducerValue) table[cellNb] = 0;
            else table[cellNb] -= reducerValue;
            cellNb++;
    }   }
}

static void ZSTD144_reduceTable(U32* const table, U32 const size, U32 const reducerValue)
{
    ZSTD144_reduceTable_internal(table, size, reducerValue, 0);
}

static void ZSTD144_reduceTable_btlazy2(U32* const table, U32 const size, U32 const reducerValue)
{
    ZSTD144_reduceTable_internal(table, size, reducerValue, 1);
}

/*! ZSTD144_reduceIndex() :
*   rescale all indexes to avoid future overflow (indexes are U32) */
static void ZSTD144_reduceIndex (ZSTD144_matchState_t* ms, ZSTD144_CCtx_params const* params, const U32 reducerValue)
{
    {   U32 const hSize = (U32)1 << params->cParams.hashLog;
        ZSTD144_reduceTable(ms->hashTable, hSize, reducerValue);
    }

    if (params->cParams.strategy != ZSTD144_fast) {
        U32 const chainSize = (U32)1 << params->cParams.chainLog;
        if (params->cParams.strategy == ZSTD144_btlazy2)
            ZSTD144_reduceTable_btlazy2(ms->chainTable, chainSize, reducerValue);
        else
            ZSTD144_reduceTable(ms->chainTable, chainSize, reducerValue);
    }

    if (ms->hashLog3) {
        U32 const h3Size = (U32)1 << ms->hashLog3;
        ZSTD144_reduceTable(ms->hashTable3, h3Size, reducerValue);
    }
}


/*-*******************************************************
*  Block entropic compression
*********************************************************/

/* See doc/zstd_compression_format.md for detailed format description */

static size_t ZSTD144_noCompressBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize, U32 lastBlock)
{
    U32 const cBlockHeader24 = lastBlock + (((U32)bt_raw)<<1) + (U32)(srcSize << 3);
    RETURN_ERROR_IF(srcSize + ZSTD144_blockHeaderSize > dstCapacity,
                    dstSize_tooSmall);
    MEM_writeLE24(dst, cBlockHeader24);
    memcpy((BYTE*)dst + ZSTD144_blockHeaderSize, src, srcSize);
    return ZSTD144_blockHeaderSize + srcSize;
}

void ZSTD144_seqToCodes(const seqStore_t* seqStorePtr)
{
    const seqDef* const sequences = seqStorePtr->sequencesStart;
    BYTE* const llCodeTable = seqStorePtr->llCode;
    BYTE* const ofCodeTable = seqStorePtr->ofCode;
    BYTE* const mlCodeTable = seqStorePtr->mlCode;
    U32 const nbSeq = (U32)(seqStorePtr->sequences - seqStorePtr->sequencesStart);
    U32 u;
    assert(nbSeq <= seqStorePtr->maxNbSeq);
    for (u=0; u<nbSeq; u++) {
        U32 const llv = sequences[u].litLength;
        U32 const mlv = sequences[u].matchLength;
        llCodeTable[u] = (BYTE)ZSTD144_LLcode(llv);
        ofCodeTable[u] = (BYTE)ZSTD144_highbit32(sequences[u].offset);
        mlCodeTable[u] = (BYTE)ZSTD144_MLcode(mlv);
    }
    if (seqStorePtr->longLengthID==1)
        llCodeTable[seqStorePtr->longLengthPos] = MaxLL;
    if (seqStorePtr->longLengthID==2)
        mlCodeTable[seqStorePtr->longLengthPos] = MaxML;
}

static int ZSTD144_disableLiteralsCompression(const ZSTD144_CCtx_params* cctxParams)
{
    switch (cctxParams->literalCompressionMode) {
    case ZSTD144_lcm_huffman:
        return 0;
    case ZSTD144_lcm_uncompressed:
        return 1;
    default:
        assert(0 /* impossible: pre-validated */);
        /* fall-through */
    case ZSTD144_lcm_auto:
        return (cctxParams->cParams.strategy == ZSTD144_fast) && (cctxParams->cParams.targetLength > 0);
    }
}

/* ZSTD144_compressSequences_internal():
 * actually compresses both literals and sequences */
MEM_STATIC size_t
ZSTD144_compressSequences_internal(seqStore_t* seqStorePtr,
                          const ZSTD144_entropyCTables_t* prevEntropy,
                                ZSTD144_entropyCTables_t* nextEntropy,
                          const ZSTD144_CCtx_params* cctxParams,
                                void* dst, size_t dstCapacity,
                                void* entropyWorkspace, size_t entropyWkspSize,
                          const int bmi2)
{
    const int longOffsets = cctxParams->cParams.windowLog > STREAM_ACCUMULATOR_MIN;
    ZSTD144_strategy const strategy = cctxParams->cParams.strategy;
    unsigned count[MaxSeq+1];
    FSE144_CTable* CTable_LitLength = nextEntropy->fse.litlengthCTable;
    FSE144_CTable* CTable_OffsetBits = nextEntropy->fse.offcodeCTable;
    FSE144_CTable* CTable_MatchLength = nextEntropy->fse.matchlengthCTable;
    U32 LLtype, Offtype, MLtype;   /* compressed, raw or rle */
    const seqDef* const sequences = seqStorePtr->sequencesStart;
    const BYTE* const ofCodeTable = seqStorePtr->ofCode;
    const BYTE* const llCodeTable = seqStorePtr->llCode;
    const BYTE* const mlCodeTable = seqStorePtr->mlCode;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    size_t const nbSeq = (size_t)(seqStorePtr->sequences - seqStorePtr->sequencesStart);
    BYTE* seqHead;
    BYTE* lastNCount = NULL;

    DEBUGLOG(5, "ZSTD144_compressSequences_internal (nbSeq=%zu)", nbSeq);
    ZSTD144_STATIC_ASSERT(HUF144_WORKSPACE_SIZE >= (1<<MAX(MLFSELog,LLFSELog)));

    /* Compress literals */
    {   const BYTE* const literals = seqStorePtr->litStart;
        size_t const litSize = (size_t)(seqStorePtr->lit - literals);
        size_t const cSize = ZSTD144_compressLiterals(
                                    &prevEntropy->huf, &nextEntropy->huf,
                                    cctxParams->cParams.strategy,
                                    ZSTD144_disableLiteralsCompression(cctxParams),
                                    op, dstCapacity,
                                    literals, litSize,
                                    entropyWorkspace, entropyWkspSize,
                                    bmi2);
        FORWARD_IF_ERROR(cSize);
        assert(cSize <= dstCapacity);
        op += cSize;
    }

    /* Sequences Header */
    RETURN_ERROR_IF((oend-op) < 3 /*max nbSeq Size*/ + 1 /*seqHead*/,
                    dstSize_tooSmall);
    if (nbSeq < 128) {
        *op++ = (BYTE)nbSeq;
    } else if (nbSeq < LONGNBSEQ) {
        op[0] = (BYTE)((nbSeq>>8) + 0x80);
        op[1] = (BYTE)nbSeq;
        op+=2;
    } else {
        op[0]=0xFF;
        MEM_writeLE16(op+1, (U16)(nbSeq - LONGNBSEQ));
        op+=3;
    }
    assert(op <= oend);
    if (nbSeq==0) {
        /* Copy the old tables over as if we repeated them */
        memcpy(&nextEntropy->fse, &prevEntropy->fse, sizeof(prevEntropy->fse));
        return (size_t)(op - ostart);
    }

    /* seqHead : flags for FSE encoding type */
    seqHead = op++;
    assert(op <= oend);

    /* convert length/distances into codes */
    ZSTD144_seqToCodes(seqStorePtr);
    /* build CTable for Literal Lengths */
    {   unsigned max = MaxLL;
        size_t const mostFrequent = HIST144_countFast_wksp(count, &max, llCodeTable, nbSeq, entropyWorkspace, entropyWkspSize);   /* can't fail */
        DEBUGLOG(5, "Building LL table");
        nextEntropy->fse.litlength_repeatMode = prevEntropy->fse.litlength_repeatMode;
        LLtype = ZSTD144_selectEncodingType(&nextEntropy->fse.litlength_repeatMode,
                                        count, max, mostFrequent, nbSeq,
                                        LLFSELog, prevEntropy->fse.litlengthCTable,
                                        LL144_defaultNorm, LL144_defaultNormLog,
                                        ZSTD144_defaultAllowed, strategy);
        assert(set_basic < set_compressed && set_rle < set_compressed);
        assert(!(LLtype < set_compressed && nextEntropy->fse.litlength_repeatMode != FSE144_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD144_buildCTable(
                op, (size_t)(oend - op),
                CTable_LitLength, LLFSELog, (symbolEncodingType_e)LLtype,
                count, max, llCodeTable, nbSeq,
                LL144_defaultNorm, LL144_defaultNormLog, MaxLL,
                prevEntropy->fse.litlengthCTable,
                sizeof(prevEntropy->fse.litlengthCTable),
                entropyWorkspace, entropyWkspSize);
            FORWARD_IF_ERROR(countSize);
            if (LLtype == set_compressed)
                lastNCount = op;
            op += countSize;
            assert(op <= oend);
    }   }
    /* build CTable for Offsets */
    {   unsigned max = MaxOff;
        size_t const mostFrequent = HIST144_countFast_wksp(
            count, &max, ofCodeTable, nbSeq, entropyWorkspace, entropyWkspSize);  /* can't fail */
        /* We can only use the basic table if max <= DefaultMaxOff, otherwise the offsets are too large */
        ZSTD144_defaultPolicy_e const defaultPolicy = (max <= DefaultMaxOff) ? ZSTD144_defaultAllowed : ZSTD144_defaultDisallowed;
        DEBUGLOG(5, "Building OF table");
        nextEntropy->fse.offcode_repeatMode = prevEntropy->fse.offcode_repeatMode;
        Offtype = ZSTD144_selectEncodingType(&nextEntropy->fse.offcode_repeatMode,
                                        count, max, mostFrequent, nbSeq,
                                        OffFSELog, prevEntropy->fse.offcodeCTable,
                                        OF144_defaultNorm, OF144_defaultNormLog,
                                        defaultPolicy, strategy);
        assert(!(Offtype < set_compressed && nextEntropy->fse.offcode_repeatMode != FSE144_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD144_buildCTable(
                op, (size_t)(oend - op),
                CTable_OffsetBits, OffFSELog, (symbolEncodingType_e)Offtype,
                count, max, ofCodeTable, nbSeq,
                OF144_defaultNorm, OF144_defaultNormLog, DefaultMaxOff,
                prevEntropy->fse.offcodeCTable,
                sizeof(prevEntropy->fse.offcodeCTable),
                entropyWorkspace, entropyWkspSize);
            FORWARD_IF_ERROR(countSize);
            if (Offtype == set_compressed)
                lastNCount = op;
            op += countSize;
            assert(op <= oend);
    }   }
    /* build CTable for MatchLengths */
    {   unsigned max = MaxML;
        size_t const mostFrequent = HIST144_countFast_wksp(
            count, &max, mlCodeTable, nbSeq, entropyWorkspace, entropyWkspSize);   /* can't fail */
        DEBUGLOG(5, "Building ML table (remaining space : %i)", (int)(oend-op));
        nextEntropy->fse.matchlength_repeatMode = prevEntropy->fse.matchlength_repeatMode;
        MLtype = ZSTD144_selectEncodingType(&nextEntropy->fse.matchlength_repeatMode,
                                        count, max, mostFrequent, nbSeq,
                                        MLFSELog, prevEntropy->fse.matchlengthCTable,
                                        ML144_defaultNorm, ML144_defaultNormLog,
                                        ZSTD144_defaultAllowed, strategy);
        assert(!(MLtype < set_compressed && nextEntropy->fse.matchlength_repeatMode != FSE144_repeat_none)); /* We don't copy tables */
        {   size_t const countSize = ZSTD144_buildCTable(
                op, (size_t)(oend - op),
                CTable_MatchLength, MLFSELog, (symbolEncodingType_e)MLtype,
                count, max, mlCodeTable, nbSeq,
                ML144_defaultNorm, ML144_defaultNormLog, MaxML,
                prevEntropy->fse.matchlengthCTable,
                sizeof(prevEntropy->fse.matchlengthCTable),
                entropyWorkspace, entropyWkspSize);
            FORWARD_IF_ERROR(countSize);
            if (MLtype == set_compressed)
                lastNCount = op;
            op += countSize;
            assert(op <= oend);
    }   }

    *seqHead = (BYTE)((LLtype<<6) + (Offtype<<4) + (MLtype<<2));

    {   size_t const bitstreamSize = ZSTD144_encodeSequences(
                                        op, (size_t)(oend - op),
                                        CTable_MatchLength, mlCodeTable,
                                        CTable_OffsetBits, ofCodeTable,
                                        CTable_LitLength, llCodeTable,
                                        sequences, nbSeq,
                                        longOffsets, bmi2);
        FORWARD_IF_ERROR(bitstreamSize);
        op += bitstreamSize;
        assert(op <= oend);
        /* zstd versions <= 1.3.4 mistakenly report corruption when
         * FSE144_readNCount() receives a buffer < 4 bytes.
         * Fixed by https://github.com/facebook/zstd/pull/1146.
         * This can happen when the last set_compressed table present is 2
         * bytes and the bitstream is only one byte.
         * In this exceedingly rare case, we will simply emit an uncompressed
         * block, since it isn't worth optimizing.
         */
        if (lastNCount && (op - lastNCount) < 4) {
            /* NCountSize >= 2 && bitstreamSize > 0 ==> lastCountSize == 3 */
            assert(op - lastNCount == 3);
            DEBUGLOG(5, "Avoiding bug in zstd decoder in versions <= 1.3.4 by "
                        "emitting an uncompressed block.");
            return 0;
        }
    }

    DEBUGLOG(5, "compressed block size : %u", (unsigned)(op - ostart));
    return (size_t)(op - ostart);
}

MEM_STATIC size_t
ZSTD144_compressSequences(seqStore_t* seqStorePtr,
                       const ZSTD144_entropyCTables_t* prevEntropy,
                             ZSTD144_entropyCTables_t* nextEntropy,
                       const ZSTD144_CCtx_params* cctxParams,
                             void* dst, size_t dstCapacity,
                             size_t srcSize,
                             void* entropyWorkspace, size_t entropyWkspSize,
                             int bmi2)
{
    size_t const cSize = ZSTD144_compressSequences_internal(
                            seqStorePtr, prevEntropy, nextEntropy, cctxParams,
                            dst, dstCapacity,
                            entropyWorkspace, entropyWkspSize, bmi2);
    if (cSize == 0) return 0;
    /* When srcSize <= dstCapacity, there is enough space to write a raw uncompressed block.
     * Since we ran out of space, block must be not compressible, so fall back to raw uncompressed block.
     */
    if ((cSize == ERROR(dstSize_tooSmall)) & (srcSize <= dstCapacity))
        return 0;  /* block not compressed */
    FORWARD_IF_ERROR(cSize);

    /* Check compressibility */
    {   size_t const maxCSize = srcSize - ZSTD144_minGain(srcSize, cctxParams->cParams.strategy);
        if (cSize >= maxCSize) return 0;  /* block not compressed */
    }

    return cSize;
}

/* ZSTD144_selectBlockCompressor() :
 * Not static, but internal use only (used by long distance matcher)
 * assumption : strat is a valid strategy */
ZSTD144_blockCompressor ZSTD144_selectBlockCompressor(ZSTD144_strategy strat, ZSTD144_dictMode_e dictMode)
{
    static const ZSTD144_blockCompressor blockCompressor[3][ZSTD144_STRATEGY_MAX+1] = {
        { ZSTD144_compressBlock_fast  /* default for 0 */,
          ZSTD144_compressBlock_fast,
          ZSTD144_compressBlock_doubleFast,
          ZSTD144_compressBlock_greedy,
          ZSTD144_compressBlock_lazy,
          ZSTD144_compressBlock_lazy2,
          ZSTD144_compressBlock_btlazy2,
          ZSTD144_compressBlock_btopt,
          ZSTD144_compressBlock_btultra,
          ZSTD144_compressBlock_btultra2 },
        { ZSTD144_compressBlock_fast_extDict  /* default for 0 */,
          ZSTD144_compressBlock_fast_extDict,
          ZSTD144_compressBlock_doubleFast_extDict,
          ZSTD144_compressBlock_greedy_extDict,
          ZSTD144_compressBlock_lazy_extDict,
          ZSTD144_compressBlock_lazy2_extDict,
          ZSTD144_compressBlock_btlazy2_extDict,
          ZSTD144_compressBlock_btopt_extDict,
          ZSTD144_compressBlock_btultra_extDict,
          ZSTD144_compressBlock_btultra_extDict },
        { ZSTD144_compressBlock_fast_dictMatchState  /* default for 0 */,
          ZSTD144_compressBlock_fast_dictMatchState,
          ZSTD144_compressBlock_doubleFast_dictMatchState,
          ZSTD144_compressBlock_greedy_dictMatchState,
          ZSTD144_compressBlock_lazy_dictMatchState,
          ZSTD144_compressBlock_lazy2_dictMatchState,
          ZSTD144_compressBlock_btlazy2_dictMatchState,
          ZSTD144_compressBlock_btopt_dictMatchState,
          ZSTD144_compressBlock_btultra_dictMatchState,
          ZSTD144_compressBlock_btultra_dictMatchState }
    };
    ZSTD144_blockCompressor selectedCompressor;
    ZSTD144_STATIC_ASSERT((unsigned)ZSTD144_fast == 1);

    assert(ZSTD144_cParam_withinBounds(ZSTD144_c_strategy, strat));
    selectedCompressor = blockCompressor[(int)dictMode][(int)strat];
    assert(selectedCompressor != NULL);
    return selectedCompressor;
}

static void ZSTD144_storeLastLiterals(seqStore_t* seqStorePtr,
                                   const BYTE* anchor, size_t lastLLSize)
{
    memcpy(seqStorePtr->lit, anchor, lastLLSize);
    seqStorePtr->lit += lastLLSize;
}

void ZSTD144_resetSeqStore(seqStore_t* ssPtr)
{
    ssPtr->lit = ssPtr->litStart;
    ssPtr->sequences = ssPtr->sequencesStart;
    ssPtr->longLengthID = 0;
}

typedef enum { ZSTDbss_compress, ZSTDbss_noCompress } ZSTD144_buildSeqStore_e;

static size_t ZSTD144_buildSeqStore(ZSTD144_CCtx* zc, const void* src, size_t srcSize)
{
    ZSTD144_matchState_t* const ms = &zc->blockState.matchState;
    DEBUGLOG(5, "ZSTD144_buildSeqStore (srcSize=%zu)", srcSize);
    assert(srcSize <= ZSTD144_BLOCKSIZE_MAX);
    /* Assert that we have correctly flushed the ctx params into the ms's copy */
    ZSTD144_assertEqualCParams(zc->appliedParams.cParams, ms->cParams);
    if (srcSize < MIN_CBLOCK_SIZE+ZSTD144_blockHeaderSize+1) {
        ZSTD144_ldm_skipSequences(&zc->externSeqStore, srcSize, zc->appliedParams.cParams.minMatch);
        return ZSTDbss_noCompress; /* don't even attempt compression below a certain srcSize */
    }
    ZSTD144_resetSeqStore(&(zc->seqStore));
    /* required for optimal parser to read stats from dictionary */
    ms->opt.symbolCosts = &zc->blockState.prevCBlock->entropy;
    /* tell the optimal parser how we expect to compress literals */
    ms->opt.literalCompressionMode = zc->appliedParams.literalCompressionMode;
    /* a gap between an attached dict and the current window is not safe,
     * they must remain adjacent,
     * and when that stops being the case, the dict must be unset */
    assert(ms->dictMatchState == NULL || ms->loadedDictEnd == ms->window.dictLimit);

    /* limited update after a very long match */
    {   const BYTE* const base = ms->window.base;
        const BYTE* const istart = (const BYTE*)src;
        const U32 current = (U32)(istart-base);
        if (sizeof(ptrdiff_t)==8) assert(istart - base < (ptrdiff_t)(U32)(-1));   /* ensure no overflow */
        if (current > ms->nextToUpdate + 384)
            ms->nextToUpdate = current - MIN(192, (U32)(current - ms->nextToUpdate - 384));
    }

    /* select and store sequences */
    {   ZSTD144_dictMode_e const dictMode = ZSTD144_matchState_dictMode(ms);
        size_t lastLLSize;
        {   int i;
            for (i = 0; i < ZSTD144_REP_NUM; ++i)
                zc->blockState.nextCBlock->rep[i] = zc->blockState.prevCBlock->rep[i];
        }
        if (zc->externSeqStore.pos < zc->externSeqStore.size) {
            assert(!zc->appliedParams.ldmParams.enableLdm);
            /* Updates ldmSeqStore.pos */
            lastLLSize =
                ZSTD144_ldm_blockCompress(&zc->externSeqStore,
                                       ms, &zc->seqStore,
                                       zc->blockState.nextCBlock->rep,
                                       src, srcSize);
            assert(zc->externSeqStore.pos <= zc->externSeqStore.size);
        } else if (zc->appliedParams.ldmParams.enableLdm) {
            rawSeqStore_t ldmSeqStore = {NULL, 0, 0, 0};

            ldmSeqStore.seq = zc->ldmSequences;
            ldmSeqStore.capacity = zc->maxNbLdmSequences;
            /* Updates ldmSeqStore.size */
            FORWARD_IF_ERROR(ZSTD144_ldm_generateSequences(&zc->ldmState, &ldmSeqStore,
                                               &zc->appliedParams.ldmParams,
                                               src, srcSize));
            /* Updates ldmSeqStore.pos */
            lastLLSize =
                ZSTD144_ldm_blockCompress(&ldmSeqStore,
                                       ms, &zc->seqStore,
                                       zc->blockState.nextCBlock->rep,
                                       src, srcSize);
            assert(ldmSeqStore.pos == ldmSeqStore.size);
        } else {   /* not long range mode */
            ZSTD144_blockCompressor const blockCompressor = ZSTD144_selectBlockCompressor(zc->appliedParams.cParams.strategy, dictMode);
            lastLLSize = blockCompressor(ms, &zc->seqStore, zc->blockState.nextCBlock->rep, src, srcSize);
        }
        {   const BYTE* const lastLiterals = (const BYTE*)src + srcSize - lastLLSize;
            ZSTD144_storeLastLiterals(&zc->seqStore, lastLiterals, lastLLSize);
    }   }
    return ZSTDbss_compress;
}

static void ZSTD144_copyBlockSequences(ZSTD144_CCtx* zc)
{
    const seqStore_t* seqStore = ZSTD144_getSeqStore(zc);
    const seqDef* seqs = seqStore->sequencesStart;
    size_t seqsSize = seqStore->sequences - seqs;

    ZSTD144_Sequence* outSeqs = &zc->seqCollector.seqStart[zc->seqCollector.seqIndex];
    size_t i; size_t position; int repIdx;

    assert(zc->seqCollector.seqIndex + 1 < zc->seqCollector.maxSequences);
    for (i = 0, position = 0; i < seqsSize; ++i) {
        outSeqs[i].offset = seqs[i].offset;
        outSeqs[i].litLength = seqs[i].litLength;
        outSeqs[i].matchLength = seqs[i].matchLength + MINMATCH;

        if (i == seqStore->longLengthPos) {
            if (seqStore->longLengthID == 1) {
                outSeqs[i].litLength += 0x10000;
            } else if (seqStore->longLengthID == 2) {
                outSeqs[i].matchLength += 0x10000;
            }
        }

        if (outSeqs[i].offset <= ZSTD144_REP_NUM) {
            outSeqs[i].rep = outSeqs[i].offset;
            repIdx = (unsigned int)i - outSeqs[i].offset;

            if (outSeqs[i].litLength == 0) {
                if (outSeqs[i].offset < 3) {
                    --repIdx;
                } else {
                    repIdx = (unsigned int)i - 1;
                }
                ++outSeqs[i].rep;
            }
            assert(repIdx >= -3);
            outSeqs[i].offset = repIdx >= 0 ? outSeqs[repIdx].offset : repStartValue[-repIdx - 1];
            if (outSeqs[i].rep == 4) {
                --outSeqs[i].offset;
            }
        } else {
            outSeqs[i].offset -= ZSTD144_REP_NUM;
        }

        position += outSeqs[i].litLength;
        outSeqs[i].matchPos = (unsigned int)position;
        position += outSeqs[i].matchLength;
    }
    zc->seqCollector.seqIndex += seqsSize;
}

size_t ZSTD144_getSequences(ZSTD144_CCtx* zc, ZSTD144_Sequence* outSeqs,
    size_t outSeqsSize, const void* src, size_t srcSize)
{
    const size_t dstCapacity = ZSTD144_compressBound(srcSize);
    void* dst = ZSTD144_malloc(dstCapacity, ZSTD144_defaultCMem);
    SeqCollector seqCollector;

    RETURN_ERROR_IF(dst == NULL, memory_allocation);

    seqCollector.collectSequences = 1;
    seqCollector.seqStart = outSeqs;
    seqCollector.seqIndex = 0;
    seqCollector.maxSequences = outSeqsSize;
    zc->seqCollector = seqCollector;

    ZSTD144_compress2(zc, dst, dstCapacity, src, srcSize);
    ZSTD144_free(dst, ZSTD144_defaultCMem);
    return zc->seqCollector.seqIndex;
}

/* Returns true if the given block is a RLE block */
static int ZSTD144_isRLE(const BYTE *ip, size_t length) {
    size_t i;
    if (length < 2) return 1;
    for (i = 1; i < length; ++i) {
        if (ip[0] != ip[i]) return 0;
    }
    return 1;
}

static size_t ZSTD144_compressBlock_internal(ZSTD144_CCtx* zc,
                                        void* dst, size_t dstCapacity,
                                        const void* src, size_t srcSize, U32 frame)
{
    /* This the upper bound for the length of an rle block.
     * This isn't the actual upper bound. Finding the real threshold
     * needs further investigation.
     */
    const U32 rleMaxLength = 25;
    size_t cSize;
    const BYTE* ip = (const BYTE*)src;
    BYTE* op = (BYTE*)dst;
    DEBUGLOG(5, "ZSTD144_compressBlock_internal (dstCapacity=%u, dictLimit=%u, nextToUpdate=%u)",
                (unsigned)dstCapacity, (unsigned)zc->blockState.matchState.window.dictLimit,
                (unsigned)zc->blockState.matchState.nextToUpdate);

    {   const size_t bss = ZSTD144_buildSeqStore(zc, src, srcSize);
        FORWARD_IF_ERROR(bss);
        if (bss == ZSTDbss_noCompress) { cSize = 0; goto out; }
    }

    if (zc->seqCollector.collectSequences) {
        ZSTD144_copyBlockSequences(zc);
        return 0;
    }

    /* encode sequences and literals */
    cSize = ZSTD144_compressSequences(&zc->seqStore,
            &zc->blockState.prevCBlock->entropy, &zc->blockState.nextCBlock->entropy,
            &zc->appliedParams,
            dst, dstCapacity,
            srcSize,
            zc->entropyWorkspace, HUF144_WORKSPACE_SIZE /* statically allocated in resetCCtx */,
            zc->bmi2);

    if (frame &&
        /* We don't want to emit our first block as a RLE even if it qualifies because
         * doing so will cause the decoder (cli only) to throw a "should consume all input error."
         * This is only an issue for zstd <= v1.4.3
         */
        !zc->isFirstBlock &&
        cSize < rleMaxLength &&
        ZSTD144_isRLE(ip, srcSize))
    {
        cSize = 1;
        op[0] = ip[0];
    }

out:
    if (!ZSTD144_isError(cSize) && cSize > 1) {
        /* confirm repcodes and entropy tables when emitting a compressed block */
        ZSTD144_compressedBlockState_t* const tmp = zc->blockState.prevCBlock;
        zc->blockState.prevCBlock = zc->blockState.nextCBlock;
        zc->blockState.nextCBlock = tmp;
    }
    /* We check that dictionaries have offset codes available for the first
     * block. After the first block, the offcode table might not have large
     * enough codes to represent the offsets in the data.
     */
    if (zc->blockState.prevCBlock->entropy.fse.offcode_repeatMode == FSE144_repeat_valid)
        zc->blockState.prevCBlock->entropy.fse.offcode_repeatMode = FSE144_repeat_check;

    return cSize;
}


static void ZSTD144_overflowCorrectIfNeeded(ZSTD144_matchState_t* ms,
                                         ZSTD144_cwksp* ws,
                                         ZSTD144_CCtx_params const* params,
                                         void const* ip,
                                         void const* iend)
{
    if (ZSTD144_window_needOverflowCorrection(ms->window, iend)) {
        U32 const maxDist = (U32)1 << params->cParams.windowLog;
        U32 const cycleLog = ZSTD144_cycleLog(params->cParams.chainLog, params->cParams.strategy);
        U32 const correction = ZSTD144_window_correctOverflow(&ms->window, cycleLog, maxDist, ip);
        ZSTD144_STATIC_ASSERT(ZSTD144_CHAINLOG_MAX <= 30);
        ZSTD144_STATIC_ASSERT(ZSTD144_WINDOWLOG_MAX_32 <= 30);
        ZSTD144_STATIC_ASSERT(ZSTD144_WINDOWLOG_MAX <= 31);
        ZSTD144_cwksp_mark_tables_dirty(ws);
        ZSTD144_reduceIndex(ms, params, correction);
        ZSTD144_cwksp_mark_tables_clean(ws);
        if (ms->nextToUpdate < correction) ms->nextToUpdate = 0;
        else ms->nextToUpdate -= correction;
        /* invalidate dictionaries on overflow correction */
        ms->loadedDictEnd = 0;
        ms->dictMatchState = NULL;
    }
}

/*! ZSTD144_compress_frameChunk() :
*   Compress a chunk of data into one or multiple blocks.
*   All blocks will be terminated, all input will be consumed.
*   Function will issue an error if there is not enough `dstCapacity` to hold the compressed content.
*   Frame is supposed already started (header already produced)
*   @return : compressed size, or an error code
*/
static size_t ZSTD144_compress_frameChunk (ZSTD144_CCtx* cctx,
                                     void* dst, size_t dstCapacity,
                               const void* src, size_t srcSize,
                                     U32 lastFrameChunk)
{
    size_t blockSize = cctx->blockSize;
    size_t remaining = srcSize;
    const BYTE* ip = (const BYTE*)src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    U32 const maxDist = (U32)1 << cctx->appliedParams.cParams.windowLog;
    assert(cctx->appliedParams.cParams.windowLog <= ZSTD144_WINDOWLOG_MAX);

    DEBUGLOG(5, "ZSTD144_compress_frameChunk (blockSize=%u)", (unsigned)blockSize);
    if (cctx->appliedParams.fParams.checksumFlag && srcSize)
        XXH_3264_update(&cctx->xxhState, src, srcSize);

    while (remaining) {
        ZSTD144_matchState_t* const ms = &cctx->blockState.matchState;
        U32 const lastBlock = lastFrameChunk & (blockSize >= remaining);

        RETURN_ERROR_IF(dstCapacity < ZSTD144_blockHeaderSize + MIN_CBLOCK_SIZE,
                        dstSize_tooSmall,
                        "not enough space to store compressed block");
        if (remaining < blockSize) blockSize = remaining;

        ZSTD144_overflowCorrectIfNeeded(
            ms, &cctx->workspace, &cctx->appliedParams, ip, ip + blockSize);
        ZSTD144_checkDictValidity(&ms->window, ip + blockSize, maxDist, &ms->loadedDictEnd, &ms->dictMatchState);

        /* Ensure hash/chain table insertion resumes no sooner than lowlimit */
        if (ms->nextToUpdate < ms->window.lowLimit) ms->nextToUpdate = ms->window.lowLimit;

        {   size_t cSize = ZSTD144_compressBlock_internal(cctx,
                                op+ZSTD144_blockHeaderSize, dstCapacity-ZSTD144_blockHeaderSize,
                                ip, blockSize, 1 /* frame */);
            FORWARD_IF_ERROR(cSize);
            if (cSize == 0) {  /* block is not compressible */
                cSize = ZSTD144_noCompressBlock(op, dstCapacity, ip, blockSize, lastBlock);
                FORWARD_IF_ERROR(cSize);
            } else {
                const U32 cBlockHeader = cSize == 1 ?
                    lastBlock + (((U32)bt_rle)<<1) + (U32)(blockSize << 3) :
                    lastBlock + (((U32)bt_compressed)<<1) + (U32)(cSize << 3);
                MEM_writeLE24(op, cBlockHeader);
                cSize += ZSTD144_blockHeaderSize;
            }

            ip += blockSize;
            assert(remaining >= blockSize);
            remaining -= blockSize;
            op += cSize;
            assert(dstCapacity >= cSize);
            dstCapacity -= cSize;
            cctx->isFirstBlock = 0;
            DEBUGLOG(5, "ZSTD144_compress_frameChunk: adding a block of size %u",
                        (unsigned)cSize);
    }   }

    if (lastFrameChunk && (op>ostart)) cctx->stage = ZSTDcs_ending;
    return (size_t)(op-ostart);
}


static size_t ZSTD144_writeFrameHeader(void* dst, size_t dstCapacity,
                                    const ZSTD144_CCtx_params* params, U64 pledgedSrcSize, U32 dictID)
{   BYTE* const op = (BYTE*)dst;
    U32   const dictIDSizeCodeLength = (dictID>0) + (dictID>=256) + (dictID>=65536);   /* 0-3 */
    U32   const dictIDSizeCode = params->fParams.noDictIDFlag ? 0 : dictIDSizeCodeLength;   /* 0-3 */
    U32   const checksumFlag = params->fParams.checksumFlag>0;
    U32   const windowSize = (U32)1 << params->cParams.windowLog;
    U32   const singleSegment = params->fParams.contentSizeFlag && (windowSize >= pledgedSrcSize);
    BYTE  const windowLogByte = (BYTE)((params->cParams.windowLog - ZSTD144_WINDOWLOG_ABSOLUTEMIN) << 3);
    U32   const fcsCode = params->fParams.contentSizeFlag ?
                     (pledgedSrcSize>=256) + (pledgedSrcSize>=65536+256) + (pledgedSrcSize>=0xFFFFFFFFU) : 0;  /* 0-3 */
    BYTE  const frameHeaderDescriptionByte = (BYTE)(dictIDSizeCode + (checksumFlag<<2) + (singleSegment<<5) + (fcsCode<<6) );
    size_t pos=0;

    assert(!(params->fParams.contentSizeFlag && pledgedSrcSize == ZSTD144_CONTENTSIZE_UNKNOWN));
    RETURN_ERROR_IF(dstCapacity < ZSTD144_FRAMEHEADERSIZE_MAX, dstSize_tooSmall);
    DEBUGLOG(4, "ZSTD144_writeFrameHeader : dictIDFlag : %u ; dictID : %u ; dictIDSizeCode : %u",
                !params->fParams.noDictIDFlag, (unsigned)dictID, (unsigned)dictIDSizeCode);

    if (params->format == ZSTD144_f_zstd1) {
        MEM_writeLE32(dst, ZSTD144_MAGICNUMBER);
        pos = 4;
    }
    op[pos++] = frameHeaderDescriptionByte;
    if (!singleSegment) op[pos++] = windowLogByte;
    switch(dictIDSizeCode)
    {
        default:  assert(0); /* impossible */
        case 0 : break;
        case 1 : op[pos] = (BYTE)(dictID); pos++; break;
        case 2 : MEM_writeLE16(op+pos, (U16)dictID); pos+=2; break;
        case 3 : MEM_writeLE32(op+pos, dictID); pos+=4; break;
    }
    switch(fcsCode)
    {
        default:  assert(0); /* impossible */
        case 0 : if (singleSegment) op[pos++] = (BYTE)(pledgedSrcSize); break;
        case 1 : MEM_writeLE16(op+pos, (U16)(pledgedSrcSize-256)); pos+=2; break;
        case 2 : MEM_writeLE32(op+pos, (U32)(pledgedSrcSize)); pos+=4; break;
        case 3 : MEM_writeLE64(op+pos, (U64)(pledgedSrcSize)); pos+=8; break;
    }
    return pos;
}

/* ZSTD144_writeLastEmptyBlock() :
 * output an empty Block with end-of-frame mark to complete a frame
 * @return : size of data written into `dst` (== ZSTD144_blockHeaderSize (defined in zstd_internal.h))
 *           or an error code if `dstCapacity` is too small (<ZSTD144_blockHeaderSize)
 */
size_t ZSTD144_writeLastEmptyBlock(void* dst, size_t dstCapacity)
{
    RETURN_ERROR_IF(dstCapacity < ZSTD144_blockHeaderSize, dstSize_tooSmall);
    {   U32 const cBlockHeader24 = 1 /*lastBlock*/ + (((U32)bt_raw)<<1);  /* 0 size */
        MEM_writeLE24(dst, cBlockHeader24);
        return ZSTD144_blockHeaderSize;
    }
}

size_t ZSTD144_referenceExternalSequences(ZSTD144_CCtx* cctx, rawSeq* seq, size_t nbSeq)
{
    RETURN_ERROR_IF(cctx->stage != ZSTDcs_init, stage_wrong);
    RETURN_ERROR_IF(cctx->appliedParams.ldmParams.enableLdm,
                    parameter_unsupported);
    cctx->externSeqStore.seq = seq;
    cctx->externSeqStore.size = nbSeq;
    cctx->externSeqStore.capacity = nbSeq;
    cctx->externSeqStore.pos = 0;
    return 0;
}


static size_t ZSTD144_compressContinue_internal (ZSTD144_CCtx* cctx,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize,
                               U32 frame, U32 lastFrameChunk)
{
    ZSTD144_matchState_t* const ms = &cctx->blockState.matchState;
    size_t fhSize = 0;

    DEBUGLOG(5, "ZSTD144_compressContinue_internal, stage: %u, srcSize: %u",
                cctx->stage, (unsigned)srcSize);
    RETURN_ERROR_IF(cctx->stage==ZSTDcs_created, stage_wrong,
                    "missing init (ZSTD144_compressBegin)");

    if (frame && (cctx->stage==ZSTDcs_init)) {
        fhSize = ZSTD144_writeFrameHeader(dst, dstCapacity, &cctx->appliedParams,
                                       cctx->pledgedSrcSizePlusOne-1, cctx->dictID);
        FORWARD_IF_ERROR(fhSize);
        assert(fhSize <= dstCapacity);
        dstCapacity -= fhSize;
        dst = (char*)dst + fhSize;
        cctx->stage = ZSTDcs_ongoing;
    }

    if (!srcSize) return fhSize;  /* do not generate an empty block if no input */

    if (!ZSTD144_window_update(&ms->window, src, srcSize)) {
        ms->nextToUpdate = ms->window.dictLimit;
    }
    if (cctx->appliedParams.ldmParams.enableLdm) {
        ZSTD144_window_update(&cctx->ldmState.window, src, srcSize);
    }

    if (!frame) {
        /* overflow check and correction for block mode */
        ZSTD144_overflowCorrectIfNeeded(
            ms, &cctx->workspace, &cctx->appliedParams,
            src, (BYTE const*)src + srcSize);
    }

    DEBUGLOG(5, "ZSTD144_compressContinue_internal (blockSize=%u)", (unsigned)cctx->blockSize);
    {   size_t const cSize = frame ?
                             ZSTD144_compress_frameChunk (cctx, dst, dstCapacity, src, srcSize, lastFrameChunk) :
                             ZSTD144_compressBlock_internal (cctx, dst, dstCapacity, src, srcSize, 0 /* frame */);
        FORWARD_IF_ERROR(cSize);
        cctx->consumedSrcSize += srcSize;
        cctx->producedCSize += (cSize + fhSize);
        assert(!(cctx->appliedParams.fParams.contentSizeFlag && cctx->pledgedSrcSizePlusOne == 0));
        if (cctx->pledgedSrcSizePlusOne != 0) {  /* control src size */
            ZSTD144_STATIC_ASSERT(ZSTD144_CONTENTSIZE_UNKNOWN == (unsigned long long)-1);
            RETURN_ERROR_IF(
                cctx->consumedSrcSize+1 > cctx->pledgedSrcSizePlusOne,
                srcSize_wrong,
                "error : pledgedSrcSize = %u, while realSrcSize >= %u",
                (unsigned)cctx->pledgedSrcSizePlusOne-1,
                (unsigned)cctx->consumedSrcSize);
        }
        return cSize + fhSize;
    }
}

size_t ZSTD144_compressContinue (ZSTD144_CCtx* cctx,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD144_compressContinue (srcSize=%u)", (unsigned)srcSize);
    return ZSTD144_compressContinue_internal(cctx, dst, dstCapacity, src, srcSize, 1 /* frame mode */, 0 /* last chunk */);
}


size_t ZSTD144_getBlockSize(const ZSTD144_CCtx* cctx)
{
    ZSTD144_compressionParameters const cParams = cctx->appliedParams.cParams;
    assert(!ZSTD144_checkCParams(cParams));
    return MIN (ZSTD144_BLOCKSIZE_MAX, (U32)1 << cParams.windowLog);
}

size_t ZSTD144_compressBlock(ZSTD144_CCtx* cctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    DEBUGLOG(5, "ZSTD144_compressBlock: srcSize = %u", (unsigned)srcSize);
    { size_t const blockSizeMax = ZSTD144_getBlockSize(cctx);
      RETURN_ERROR_IF(srcSize > blockSizeMax, srcSize_wrong); }

    return ZSTD144_compressContinue_internal(cctx, dst, dstCapacity, src, srcSize, 0 /* frame mode */, 0 /* last chunk */);
}

/*! ZSTD144_loadDictionaryContent() :
 *  @return : 0, or an error code
 */
static size_t ZSTD144_loadDictionaryContent(ZSTD144_matchState_t* ms,
                                         ZSTD144_cwksp* ws,
                                         ZSTD144_CCtx_params const* params,
                                         const void* src, size_t srcSize,
                                         ZSTD144_dictTableLoadMethod_e dtlm)
{
    const BYTE* ip = (const BYTE*) src;
    const BYTE* const iend = ip + srcSize;

    ZSTD144_window_update(&ms->window, src, srcSize);
    ms->loadedDictEnd = params->forceWindow ? 0 : (U32)(iend - ms->window.base);

    /* Assert that we the ms params match the params we're being given */
    ZSTD144_assertEqualCParams(params->cParams, ms->cParams);

    if (srcSize <= HASH_READ_SIZE) return 0;

    while (iend - ip > HASH_READ_SIZE) {
        size_t const remaining = (size_t)(iend - ip);
        size_t const chunk = MIN(remaining, ZSTD144_CHUNKSIZE_MAX);
        const BYTE* const ichunk = ip + chunk;

        ZSTD144_overflowCorrectIfNeeded(ms, ws, params, ip, ichunk);

        switch(params->cParams.strategy)
        {
        case ZSTD144_fast:
            ZSTD144_fillHashTable(ms, ichunk, dtlm);
            break;
        case ZSTD144_dfast:
            ZSTD144_fillDoubleHashTable(ms, ichunk, dtlm);
            break;

        case ZSTD144_greedy:
        case ZSTD144_lazy:
        case ZSTD144_lazy2:
            if (chunk >= HASH_READ_SIZE)
                ZSTD144_insertAndFindFirstIndex(ms, ichunk-HASH_READ_SIZE);
            break;

        case ZSTD144_btlazy2:   /* we want the dictionary table fully sorted */
        case ZSTD144_btopt:
        case ZSTD144_btultra:
        case ZSTD144_btultra2:
            if (chunk >= HASH_READ_SIZE)
                ZSTD144_updateTree(ms, ichunk-HASH_READ_SIZE, ichunk);
            break;

        default:
            assert(0);  /* not possible : not a valid strategy id */
        }

        ip = ichunk;
    }

    ms->nextToUpdate = (U32)(iend - ms->window.base);
    return 0;
}


/* Dictionaries that assign zero probability to symbols that show up causes problems
   when FSE encoding.  Refuse dictionaries that assign zero probability to symbols
   that we may encounter during compression.
   NOTE: This behavior is not standard and could be improved in the future. */
static size_t ZSTD144_checkDictNCount(short* normalizedCounter, unsigned dictMaxSymbolValue, unsigned maxSymbolValue) {
    U32 s;
    RETURN_ERROR_IF(dictMaxSymbolValue < maxSymbolValue, dictionary_corrupted);
    for (s = 0; s <= maxSymbolValue; ++s) {
        RETURN_ERROR_IF(normalizedCounter[s] == 0, dictionary_corrupted);
    }
    return 0;
}


/* Dictionary format :
 * See :
 * https://github.com/facebook/zstd/blob/master/doc/zstd_compression_format.md#dictionary-format
 */
/*! ZSTD144_loadZstdDictionary() :
 * @return : dictID, or an error code
 *  assumptions : magic number supposed already checked
 *                dictSize supposed >= 8
 */
static size_t ZSTD144_loadZstdDictionary(ZSTD144_compressedBlockState_t* bs,
                                      ZSTD144_matchState_t* ms,
                                      ZSTD144_cwksp* ws,
                                      ZSTD144_CCtx_params const* params,
                                      const void* dict, size_t dictSize,
                                      ZSTD144_dictTableLoadMethod_e dtlm,
                                      void* workspace)
{
    const BYTE* dictPtr = (const BYTE*)dict;
    const BYTE* const dictEnd = dictPtr + dictSize;
    short offcodeNCount[MaxOff+1];
    unsigned offcodeMaxValue = MaxOff;
    size_t dictID;

    ZSTD144_STATIC_ASSERT(HUF144_WORKSPACE_SIZE >= (1<<MAX(MLFSELog,LLFSELog)));
    assert(dictSize >= 8);
    assert(MEM_readLE32(dictPtr) == ZSTD144_MAGIC_DICTIONARY);

    dictPtr += 4;   /* skip magic number */
    dictID = params->fParams.noDictIDFlag ? 0 :  MEM_readLE32(dictPtr);
    dictPtr += 4;

    {   unsigned maxSymbolValue = 255;
        size_t const hufHeaderSize = HUF144_readCTable((HUF144_CElt*)bs->entropy.huf.CTable, &maxSymbolValue, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(HUF144_isError(hufHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(maxSymbolValue < 255, dictionary_corrupted);
        dictPtr += hufHeaderSize;
    }

    {   unsigned offcodeLog;
        size_t const offcodeHeaderSize = FSE144_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(offcodeHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(offcodeLog > OffFSELog, dictionary_corrupted);
        /* Defer checking offcodeMaxValue because we need to know the size of the dictionary content */
        /* fill all offset symbols to avoid garbage at end of table */
        RETURN_ERROR_IF(FSE144_isError(FSE144_buildCTable_wksp(
                bs->entropy.fse.offcodeCTable,
                offcodeNCount, MaxOff, offcodeLog,
                workspace, HUF144_WORKSPACE_SIZE)),
            dictionary_corrupted);
        dictPtr += offcodeHeaderSize;
    }

    {   short matchlengthNCount[MaxML+1];
        unsigned matchlengthMaxValue = MaxML, matchlengthLog;
        size_t const matchlengthHeaderSize = FSE144_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(matchlengthHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(matchlengthLog > MLFSELog, dictionary_corrupted);
        /* Every match length code must have non-zero probability */
        FORWARD_IF_ERROR( ZSTD144_checkDictNCount(matchlengthNCount, matchlengthMaxValue, MaxML));
        RETURN_ERROR_IF(FSE144_isError(FSE144_buildCTable_wksp(
                bs->entropy.fse.matchlengthCTable,
                matchlengthNCount, matchlengthMaxValue, matchlengthLog,
                workspace, HUF144_WORKSPACE_SIZE)),
            dictionary_corrupted);
        dictPtr += matchlengthHeaderSize;
    }

    {   short litlengthNCount[MaxLL+1];
        unsigned litlengthMaxValue = MaxLL, litlengthLog;
        size_t const litlengthHeaderSize = FSE144_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd-dictPtr);
        RETURN_ERROR_IF(FSE144_isError(litlengthHeaderSize), dictionary_corrupted);
        RETURN_ERROR_IF(litlengthLog > LLFSELog, dictionary_corrupted);
        /* Every literal length code must have non-zero probability */
        FORWARD_IF_ERROR( ZSTD144_checkDictNCount(litlengthNCount, litlengthMaxValue, MaxLL));
        RETURN_ERROR_IF(FSE144_isError(FSE144_buildCTable_wksp(
                bs->entropy.fse.litlengthCTable,
                litlengthNCount, litlengthMaxValue, litlengthLog,
                workspace, HUF144_WORKSPACE_SIZE)),
            dictionary_corrupted);
        dictPtr += litlengthHeaderSize;
    }

    RETURN_ERROR_IF(dictPtr+12 > dictEnd, dictionary_corrupted);
    bs->rep[0] = MEM_readLE32(dictPtr+0);
    bs->rep[1] = MEM_readLE32(dictPtr+4);
    bs->rep[2] = MEM_readLE32(dictPtr+8);
    dictPtr += 12;

    {   size_t const dictContentSize = (size_t)(dictEnd - dictPtr);
        U32 offcodeMax = MaxOff;
        if (dictContentSize <= ((U32)-1) - 128 KB) {
            U32 const maxOffset = (U32)dictContentSize + 128 KB; /* The maximum offset that must be supported */
            offcodeMax = ZSTD144_highbit32(maxOffset); /* Calculate minimum offset code required to represent maxOffset */
        }
        /* All offset values <= dictContentSize + 128 KB must be representable */
        FORWARD_IF_ERROR(ZSTD144_checkDictNCount(offcodeNCount, offcodeMaxValue, MIN(offcodeMax, MaxOff)));
        /* All repCodes must be <= dictContentSize and != 0*/
        {   U32 u;
            for (u=0; u<3; u++) {
                RETURN_ERROR_IF(bs->rep[u] == 0, dictionary_corrupted);
                RETURN_ERROR_IF(bs->rep[u] > dictContentSize, dictionary_corrupted);
        }   }

        bs->entropy.huf.repeatMode = HUF144_repeat_valid;
        bs->entropy.fse.offcode_repeatMode = FSE144_repeat_valid;
        bs->entropy.fse.matchlength_repeatMode = FSE144_repeat_valid;
        bs->entropy.fse.litlength_repeatMode = FSE144_repeat_valid;
        FORWARD_IF_ERROR(ZSTD144_loadDictionaryContent(
            ms, ws, params, dictPtr, dictContentSize, dtlm));
        return dictID;
    }
}

/** ZSTD144_compress_insertDictionary() :
*   @return : dictID, or an error code */
static size_t
ZSTD144_compress_insertDictionary(ZSTD144_compressedBlockState_t* bs,
                               ZSTD144_matchState_t* ms,
                               ZSTD144_cwksp* ws,
                         const ZSTD144_CCtx_params* params,
                         const void* dict, size_t dictSize,
                               ZSTD144_dictContentType_e dictContentType,
                               ZSTD144_dictTableLoadMethod_e dtlm,
                               void* workspace)
{
    DEBUGLOG(4, "ZSTD144_compress_insertDictionary (dictSize=%u)", (U32)dictSize);
    if ((dict==NULL) || (dictSize<8)) {
        RETURN_ERROR_IF(dictContentType == ZSTD144_dct_fullDict, dictionary_wrong);
        return 0;
    }

    ZSTD144_reset_compressedBlockState(bs);

    /* dict restricted modes */
    if (dictContentType == ZSTD144_dct_rawContent)
        return ZSTD144_loadDictionaryContent(ms, ws, params, dict, dictSize, dtlm);

    if (MEM_readLE32(dict) != ZSTD144_MAGIC_DICTIONARY) {
        if (dictContentType == ZSTD144_dct_auto) {
            DEBUGLOG(4, "raw content dictionary detected");
            return ZSTD144_loadDictionaryContent(
                ms, ws, params, dict, dictSize, dtlm);
        }
        RETURN_ERROR_IF(dictContentType == ZSTD144_dct_fullDict, dictionary_wrong);
        assert(0);   /* impossible */
    }

    /* dict as full zstd dictionary */
    return ZSTD144_loadZstdDictionary(
        bs, ms, ws, params, dict, dictSize, dtlm, workspace);
}

#define ZSTD144_USE_CDICT_PARAMS_SRCSIZE_CUTOFF (128 KB)
#define ZSTD144_USE_CDICT_PARAMS_DICTSIZE_MULTIPLIER (6)

/*! ZSTD144_compressBegin_internal() :
 * @return : 0, or an error code */
static size_t ZSTD144_compressBegin_internal(ZSTD144_CCtx* cctx,
                                    const void* dict, size_t dictSize,
                                    ZSTD144_dictContentType_e dictContentType,
                                    ZSTD144_dictTableLoadMethod_e dtlm,
                                    const ZSTD144_CDict* cdict,
                                    const ZSTD144_CCtx_params* params, U64 pledgedSrcSize,
                                    ZSTD144_buffered_policy_e zbuff)
{
    DEBUGLOG(4, "ZSTD144_compressBegin_internal: wlog=%u", params->cParams.windowLog);
    /* params are supposed to be fully validated at this point */
    assert(!ZSTD144_isError(ZSTD144_checkCParams(params->cParams)));
    assert(!((dict) && (cdict)));  /* either dict or cdict, not both */
    if ( (cdict)
      && (cdict->dictContentSize > 0)
      && ( pledgedSrcSize < ZSTD144_USE_CDICT_PARAMS_SRCSIZE_CUTOFF
        || pledgedSrcSize < cdict->dictContentSize * ZSTD144_USE_CDICT_PARAMS_DICTSIZE_MULTIPLIER
        || pledgedSrcSize == ZSTD144_CONTENTSIZE_UNKNOWN
        || cdict->compressionLevel == 0)
      && (params->attachDictPref != ZSTD144_dictForceLoad) ) {
        return ZSTD144_resetCCtx_usingCDict(cctx, cdict, params, pledgedSrcSize, zbuff);
    }

    FORWARD_IF_ERROR( ZSTD144_resetCCtx_internal(cctx, *params, pledgedSrcSize,
                                     ZSTDcrp_makeClean, zbuff) );
    {   size_t const dictID = cdict ?
                ZSTD144_compress_insertDictionary(
                        cctx->blockState.prevCBlock, &cctx->blockState.matchState,
                        &cctx->workspace, params, cdict->dictContent, cdict->dictContentSize,
                        dictContentType, dtlm, cctx->entropyWorkspace)
              : ZSTD144_compress_insertDictionary(
                        cctx->blockState.prevCBlock, &cctx->blockState.matchState,
                        &cctx->workspace, params, dict, dictSize,
                        dictContentType, dtlm, cctx->entropyWorkspace);
        FORWARD_IF_ERROR(dictID);
        assert(dictID <= UINT_MAX);
        cctx->dictID = (U32)dictID;
    }
    return 0;
}

size_t ZSTD144_compressBegin_advanced_internal(ZSTD144_CCtx* cctx,
                                    const void* dict, size_t dictSize,
                                    ZSTD144_dictContentType_e dictContentType,
                                    ZSTD144_dictTableLoadMethod_e dtlm,
                                    const ZSTD144_CDict* cdict,
                                    const ZSTD144_CCtx_params* params,
                                    unsigned long long pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_compressBegin_advanced_internal: wlog=%u", params->cParams.windowLog);
    /* compression parameters verification and optimization */
    FORWARD_IF_ERROR( ZSTD144_checkCParams(params->cParams) );
    return ZSTD144_compressBegin_internal(cctx,
                                       dict, dictSize, dictContentType, dtlm,
                                       cdict,
                                       params, pledgedSrcSize,
                                       ZSTDb_not_buffered);
}

/*! ZSTD144_compressBegin_advanced() :
*   @return : 0, or an error code */
size_t ZSTD144_compressBegin_advanced(ZSTD144_CCtx* cctx,
                             const void* dict, size_t dictSize,
                                   ZSTD144_parameters params, unsigned long long pledgedSrcSize)
{
    ZSTD144_CCtx_params const cctxParams =
            ZSTD144_assignParamsToCCtxParams(&cctx->requestedParams, params);
    return ZSTD144_compressBegin_advanced_internal(cctx,
                                            dict, dictSize, ZSTD144_dct_auto, ZSTD144_dtlm_fast,
                                            NULL /*cdict*/,
                                            &cctxParams, pledgedSrcSize);
}

size_t ZSTD144_compressBegin_usingDict(ZSTD144_CCtx* cctx, const void* dict, size_t dictSize, int compressionLevel)
{
    ZSTD144_parameters const params = ZSTD144_getParams(compressionLevel, ZSTD144_CONTENTSIZE_UNKNOWN, dictSize);
    ZSTD144_CCtx_params const cctxParams =
            ZSTD144_assignParamsToCCtxParams(&cctx->requestedParams, params);
    DEBUGLOG(4, "ZSTD144_compressBegin_usingDict (dictSize=%u)", (unsigned)dictSize);
    return ZSTD144_compressBegin_internal(cctx, dict, dictSize, ZSTD144_dct_auto, ZSTD144_dtlm_fast, NULL,
                                       &cctxParams, ZSTD144_CONTENTSIZE_UNKNOWN, ZSTDb_not_buffered);
}

size_t ZSTD144_compressBegin(ZSTD144_CCtx* cctx, int compressionLevel)
{
    return ZSTD144_compressBegin_usingDict(cctx, NULL, 0, compressionLevel);
}


/*! ZSTD144_writeEpilogue() :
*   Ends a frame.
*   @return : nb of bytes written into dst (or an error code) */
static size_t ZSTD144_writeEpilogue(ZSTD144_CCtx* cctx, void* dst, size_t dstCapacity)
{
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    size_t fhSize = 0;

    DEBUGLOG(4, "ZSTD144_writeEpilogue");
    RETURN_ERROR_IF(cctx->stage == ZSTDcs_created, stage_wrong, "init missing");

    /* special case : empty frame */
    if (cctx->stage == ZSTDcs_init) {
        fhSize = ZSTD144_writeFrameHeader(dst, dstCapacity, &cctx->appliedParams, 0, 0);
        FORWARD_IF_ERROR(fhSize);
        dstCapacity -= fhSize;
        op += fhSize;
        cctx->stage = ZSTDcs_ongoing;
    }

    if (cctx->stage != ZSTDcs_ending) {
        /* write one last empty block, make it the "last" block */
        U32 const cBlockHeader24 = 1 /* last block */ + (((U32)bt_raw)<<1) + 0;
        RETURN_ERROR_IF(dstCapacity<4, dstSize_tooSmall);
        MEM_writeLE32(op, cBlockHeader24);
        op += ZSTD144_blockHeaderSize;
        dstCapacity -= ZSTD144_blockHeaderSize;
    }

    if (cctx->appliedParams.fParams.checksumFlag) {
        U32 const checksum = (U32) XXH_3264_digest(&cctx->xxhState);
        RETURN_ERROR_IF(dstCapacity<4, dstSize_tooSmall);
        DEBUGLOG(4, "ZSTD144_writeEpilogue: write checksum : %08X", (unsigned)checksum);
        MEM_writeLE32(op, checksum);
        op += 4;
    }

    cctx->stage = ZSTDcs_created;  /* return to "created but no init" status */
    return op-ostart;
}

size_t ZSTD144_compressEnd (ZSTD144_CCtx* cctx,
                         void* dst, size_t dstCapacity,
                   const void* src, size_t srcSize)
{
    size_t endResult;
    size_t const cSize = ZSTD144_compressContinue_internal(cctx,
                                dst, dstCapacity, src, srcSize,
                                1 /* frame mode */, 1 /* last chunk */);
    FORWARD_IF_ERROR(cSize);
    endResult = ZSTD144_writeEpilogue(cctx, (char*)dst + cSize, dstCapacity-cSize);
    FORWARD_IF_ERROR(endResult);
    assert(!(cctx->appliedParams.fParams.contentSizeFlag && cctx->pledgedSrcSizePlusOne == 0));
    if (cctx->pledgedSrcSizePlusOne != 0) {  /* control src size */
        ZSTD144_STATIC_ASSERT(ZSTD144_CONTENTSIZE_UNKNOWN == (unsigned long long)-1);
        DEBUGLOG(4, "end of frame : controlling src size");
        RETURN_ERROR_IF(
            cctx->pledgedSrcSizePlusOne != cctx->consumedSrcSize+1,
            srcSize_wrong,
             "error : pledgedSrcSize = %u, while realSrcSize = %u",
            (unsigned)cctx->pledgedSrcSizePlusOne-1,
            (unsigned)cctx->consumedSrcSize);
    }
    return cSize + endResult;
}


static size_t ZSTD144_compress_internal (ZSTD144_CCtx* cctx,
                                      void* dst, size_t dstCapacity,
                                const void* src, size_t srcSize,
                                const void* dict,size_t dictSize,
                                      ZSTD144_parameters params)
{
    ZSTD144_CCtx_params const cctxParams =
            ZSTD144_assignParamsToCCtxParams(&cctx->requestedParams, params);
    DEBUGLOG(4, "ZSTD144_compress_internal");
    return ZSTD144_compress_advanced_internal(cctx,
                                           dst, dstCapacity,
                                           src, srcSize,
                                           dict, dictSize,
                                           &cctxParams);
}

size_t ZSTD144_compress_advanced (ZSTD144_CCtx* cctx,
                               void* dst, size_t dstCapacity,
                         const void* src, size_t srcSize,
                         const void* dict,size_t dictSize,
                               ZSTD144_parameters params)
{
    DEBUGLOG(4, "ZSTD144_compress_advanced");
    FORWARD_IF_ERROR(ZSTD144_checkCParams(params.cParams));
    return ZSTD144_compress_internal(cctx,
                                  dst, dstCapacity,
                                  src, srcSize,
                                  dict, dictSize,
                                  params);
}

/* Internal */
size_t ZSTD144_compress_advanced_internal(
        ZSTD144_CCtx* cctx,
        void* dst, size_t dstCapacity,
        const void* src, size_t srcSize,
        const void* dict,size_t dictSize,
        const ZSTD144_CCtx_params* params)
{
    DEBUGLOG(4, "ZSTD144_compress_advanced_internal (srcSize:%u)", (unsigned)srcSize);
    FORWARD_IF_ERROR( ZSTD144_compressBegin_internal(cctx,
                         dict, dictSize, ZSTD144_dct_auto, ZSTD144_dtlm_fast, NULL,
                         params, srcSize, ZSTDb_not_buffered) );
    return ZSTD144_compressEnd(cctx, dst, dstCapacity, src, srcSize);
}

size_t ZSTD144_compress_usingDict(ZSTD144_CCtx* cctx,
                               void* dst, size_t dstCapacity,
                         const void* src, size_t srcSize,
                         const void* dict, size_t dictSize,
                               int compressionLevel)
{
    ZSTD144_parameters const params = ZSTD144_getParams(compressionLevel, srcSize + (!srcSize), dict ? dictSize : 0);
    ZSTD144_CCtx_params cctxParams = ZSTD144_assignParamsToCCtxParams(&cctx->requestedParams, params);
    assert(params.fParams.contentSizeFlag == 1);
    return ZSTD144_compress_advanced_internal(cctx, dst, dstCapacity, src, srcSize, dict, dictSize, &cctxParams);
}

size_t ZSTD144_compressCCtx(ZSTD144_CCtx* cctx,
                         void* dst, size_t dstCapacity,
                   const void* src, size_t srcSize,
                         int compressionLevel)
{
    DEBUGLOG(4, "ZSTD144_compressCCtx (srcSize=%u)", (unsigned)srcSize);
    assert(cctx != NULL);
    return ZSTD144_compress_usingDict(cctx, dst, dstCapacity, src, srcSize, NULL, 0, compressionLevel);
}

size_t ZSTD144_compress(void* dst, size_t dstCapacity,
               const void* src, size_t srcSize,
                     int compressionLevel)
{
    size_t result;
    ZSTD144_CCtx ctxBody;
    ZSTD144_initCCtx(&ctxBody, ZSTD144_defaultCMem);
    result = ZSTD144_compressCCtx(&ctxBody, dst, dstCapacity, src, srcSize, compressionLevel);
    ZSTD144_freeCCtxContent(&ctxBody);   /* can't free ctxBody itself, as it's on stack; free only heap content */
    return result;
}


/* =====  Dictionary API  ===== */

/*! ZSTD144_estimateCDictSize_advanced() :
 *  Estimate amount of memory that will be needed to create a dictionary with following arguments */
size_t ZSTD144_estimateCDictSize_advanced(
        size_t dictSize, ZSTD144_compressionParameters cParams,
        ZSTD144_dictLoadMethod_e dictLoadMethod)
{
    DEBUGLOG(5, "sizeof(ZSTD144_CDict) : %u", (unsigned)sizeof(ZSTD144_CDict));
    return ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_CDict))
         + ZSTD144_cwksp_alloc_size(HUF144_WORKSPACE_SIZE)
         + ZSTD144_sizeof_matchState(&cParams, /* forCCtx */ 0)
         + (dictLoadMethod == ZSTD144_dlm_byRef ? 0
            : ZSTD144_cwksp_alloc_size(ZSTD144_cwksp_align(dictSize, sizeof(void *))));
}

size_t ZSTD144_estimateCDictSize(size_t dictSize, int compressionLevel)
{
    ZSTD144_compressionParameters const cParams = ZSTD144_getCParams(compressionLevel, 0, dictSize);
    return ZSTD144_estimateCDictSize_advanced(dictSize, cParams, ZSTD144_dlm_byCopy);
}

size_t ZSTD144_sizeof_CDict(const ZSTD144_CDict* cdict)
{
    if (cdict==NULL) return 0;   /* support sizeof on NULL */
    DEBUGLOG(5, "sizeof(*cdict) : %u", (unsigned)sizeof(*cdict));
    /* cdict may be in the workspace */
    return (cdict->workspace.workspace == cdict ? 0 : sizeof(*cdict))
        + ZSTD144_cwksp_sizeof(&cdict->workspace);
}

static size_t ZSTD144_initCDict_internal(
                    ZSTD144_CDict* cdict,
              const void* dictBuffer, size_t dictSize,
                    ZSTD144_dictLoadMethod_e dictLoadMethod,
                    ZSTD144_dictContentType_e dictContentType,
                    ZSTD144_compressionParameters cParams)
{
    DEBUGLOG(3, "ZSTD144_initCDict_internal (dictContentType:%u)", (unsigned)dictContentType);
    assert(!ZSTD144_checkCParams(cParams));
    cdict->matchState.cParams = cParams;
    if ((dictLoadMethod == ZSTD144_dlm_byRef) || (!dictBuffer) || (!dictSize)) {
        cdict->dictContent = dictBuffer;
    } else {
         void *internalBuffer = ZSTD144_cwksp_reserve_object(&cdict->workspace, ZSTD144_cwksp_align(dictSize, sizeof(void*)));
        RETURN_ERROR_IF(!internalBuffer, memory_allocation);
        cdict->dictContent = internalBuffer;
        memcpy(internalBuffer, dictBuffer, dictSize);
    }
    cdict->dictContentSize = dictSize;

    cdict->entropyWorkspace = (U32*)ZSTD144_cwksp_reserve_object(&cdict->workspace, HUF144_WORKSPACE_SIZE);


    /* Reset the state to no dictionary */
    ZSTD144_reset_compressedBlockState(&cdict->cBlockState);
    FORWARD_IF_ERROR(ZSTD144_reset_matchState(
        &cdict->matchState,
        &cdict->workspace,
        &cParams,
        ZSTDcrp_makeClean,
        ZSTDirp_reset,
        ZSTD144_resetTarget_CDict));
    /* (Maybe) load the dictionary
     * Skips loading the dictionary if it is < 8 bytes.
     */
    {   ZSTD144_CCtx_params params;
        memset(&params, 0, sizeof(params));
        params.compressionLevel = ZSTD144_CLEVEL_DEFAULT;
        params.fParams.contentSizeFlag = 1;
        params.cParams = cParams;
        {   size_t const dictID = ZSTD144_compress_insertDictionary(
                    &cdict->cBlockState, &cdict->matchState, &cdict->workspace,
                    &params, cdict->dictContent, cdict->dictContentSize,
                    dictContentType, ZSTD144_dtlm_full, cdict->entropyWorkspace);
            FORWARD_IF_ERROR(dictID);
            assert(dictID <= (size_t)(U32)-1);
            cdict->dictID = (U32)dictID;
        }
    }

    return 0;
}

ZSTD144_CDict* ZSTD144_createCDict_advanced(const void* dictBuffer, size_t dictSize,
                                      ZSTD144_dictLoadMethod_e dictLoadMethod,
                                      ZSTD144_dictContentType_e dictContentType,
                                      ZSTD144_compressionParameters cParams, ZSTD144_customMem customMem)
{
    DEBUGLOG(3, "ZSTD144_createCDict_advanced, mode %u", (unsigned)dictContentType);
    if (!customMem.customAlloc ^ !customMem.customFree) return NULL;

    {   size_t const workspaceSize =
            ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_CDict)) +
            ZSTD144_cwksp_alloc_size(HUF144_WORKSPACE_SIZE) +
            ZSTD144_sizeof_matchState(&cParams, /* forCCtx */ 0) +
            (dictLoadMethod == ZSTD144_dlm_byRef ? 0
             : ZSTD144_cwksp_alloc_size(ZSTD144_cwksp_align(dictSize, sizeof(void*))));
        void* const workspace = ZSTD144_malloc(workspaceSize, customMem);
        ZSTD144_cwksp ws;
        ZSTD144_CDict* cdict;

        if (!workspace) {
            ZSTD144_free(workspace, customMem);
            return NULL;
        }

        ZSTD144_cwksp_init(&ws, workspace, workspaceSize);

        cdict = (ZSTD144_CDict*)ZSTD144_cwksp_reserve_object(&ws, sizeof(ZSTD144_CDict));
        assert(cdict != NULL);
        ZSTD144_cwksp_move(&cdict->workspace, &ws);
        cdict->customMem = customMem;
        cdict->compressionLevel = 0; /* signals advanced API usage */

        if (ZSTD144_isError( ZSTD144_initCDict_internal(cdict,
                                        dictBuffer, dictSize,
                                        dictLoadMethod, dictContentType,
                                        cParams) )) {
            ZSTD144_freeCDict(cdict);
            return NULL;
        }

        return cdict;
    }
}

ZSTD144_CDict* ZSTD144_createCDict(const void* dict, size_t dictSize, int compressionLevel)
{
    ZSTD144_compressionParameters cParams = ZSTD144_getCParams(compressionLevel, 0, dictSize);
    ZSTD144_CDict* cdict = ZSTD144_createCDict_advanced(dict, dictSize,
                                                  ZSTD144_dlm_byCopy, ZSTD144_dct_auto,
                                                  cParams, ZSTD144_defaultCMem);
    if (cdict)
        cdict->compressionLevel = compressionLevel == 0 ? ZSTD144_CLEVEL_DEFAULT : compressionLevel;
    return cdict;
}

ZSTD144_CDict* ZSTD144_createCDict_byReference(const void* dict, size_t dictSize, int compressionLevel)
{
    ZSTD144_compressionParameters cParams = ZSTD144_getCParams(compressionLevel, 0, dictSize);
    return ZSTD144_createCDict_advanced(dict, dictSize,
                                     ZSTD144_dlm_byRef, ZSTD144_dct_auto,
                                     cParams, ZSTD144_defaultCMem);
}

size_t ZSTD144_freeCDict(ZSTD144_CDict* cdict)
{
    if (cdict==NULL) return 0;   /* support free on NULL */
    {   ZSTD144_customMem const cMem = cdict->customMem;
        int cdictInWorkspace = ZSTD144_cwksp_owns_buffer(&cdict->workspace, cdict);
        ZSTD144_cwksp_free(&cdict->workspace, cMem);
        if (!cdictInWorkspace) {
            ZSTD144_free(cdict, cMem);
        }
        return 0;
    }
}

/*! ZSTD144_initStaticCDict_advanced() :
 *  Generate a digested dictionary in provided memory area.
 *  workspace: The memory area to emplace the dictionary into.
 *             Provided pointer must 8-bytes aligned.
 *             It must outlive dictionary usage.
 *  workspaceSize: Use ZSTD144_estimateCDictSize()
 *                 to determine how large workspace must be.
 *  cParams : use ZSTD144_getCParams() to transform a compression level
 *            into its relevants cParams.
 * @return : pointer to ZSTD144_CDict*, or NULL if error (size too small)
 *  Note : there is no corresponding "free" function.
 *         Since workspace was allocated externally, it must be freed externally.
 */
const ZSTD144_CDict* ZSTD144_initStaticCDict(
                                 void* workspace, size_t workspaceSize,
                           const void* dict, size_t dictSize,
                                 ZSTD144_dictLoadMethod_e dictLoadMethod,
                                 ZSTD144_dictContentType_e dictContentType,
                                 ZSTD144_compressionParameters cParams)
{
    size_t const matchStateSize = ZSTD144_sizeof_matchState(&cParams, /* forCCtx */ 0);
    size_t const neededSize = ZSTD144_cwksp_alloc_size(sizeof(ZSTD144_CDict))
                            + (dictLoadMethod == ZSTD144_dlm_byRef ? 0
                               : ZSTD144_cwksp_alloc_size(ZSTD144_cwksp_align(dictSize, sizeof(void*))))
                            + ZSTD144_cwksp_alloc_size(HUF144_WORKSPACE_SIZE)
                            + matchStateSize;
    ZSTD144_CDict* cdict;

    if ((size_t)workspace & 7) return NULL;  /* 8-aligned */

    {
        ZSTD144_cwksp ws;
        ZSTD144_cwksp_init(&ws, workspace, workspaceSize);
        cdict = (ZSTD144_CDict*)ZSTD144_cwksp_reserve_object(&ws, sizeof(ZSTD144_CDict));
        if (cdict == NULL) return NULL;
        ZSTD144_cwksp_move(&cdict->workspace, &ws);
    }

    DEBUGLOG(4, "(workspaceSize < neededSize) : (%u < %u) => %u",
        (unsigned)workspaceSize, (unsigned)neededSize, (unsigned)(workspaceSize < neededSize));
    if (workspaceSize < neededSize) return NULL;

    if (ZSTD144_isError( ZSTD144_initCDict_internal(cdict,
                                              dict, dictSize,
                                              dictLoadMethod, dictContentType,
                                              cParams) ))
        return NULL;

    return cdict;
}

ZSTD144_compressionParameters ZSTD144_getCParamsFromCDict(const ZSTD144_CDict* cdict)
{
    assert(cdict != NULL);
    return cdict->matchState.cParams;
}

/* ZSTD144_compressBegin_usingCDict_advanced() :
 * cdict must be != NULL */
size_t ZSTD144_compressBegin_usingCDict_advanced(
    ZSTD144_CCtx* const cctx, const ZSTD144_CDict* const cdict,
    ZSTD144_frameParameters const fParams, unsigned long long const pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_compressBegin_usingCDict_advanced");
    RETURN_ERROR_IF(cdict==NULL, dictionary_wrong);
    {   ZSTD144_CCtx_params params = cctx->requestedParams;
        params.cParams = ( pledgedSrcSize < ZSTD144_USE_CDICT_PARAMS_SRCSIZE_CUTOFF
                        || pledgedSrcSize < cdict->dictContentSize * ZSTD144_USE_CDICT_PARAMS_DICTSIZE_MULTIPLIER
                        || pledgedSrcSize == ZSTD144_CONTENTSIZE_UNKNOWN
                        || cdict->compressionLevel == 0 )
                      && (params.attachDictPref != ZSTD144_dictForceLoad) ?
                ZSTD144_getCParamsFromCDict(cdict)
              : ZSTD144_getCParams(cdict->compressionLevel,
                                pledgedSrcSize,
                                cdict->dictContentSize);
        /* Increase window log to fit the entire dictionary and source if the
         * source size is known. Limit the increase to 19, which is the
         * window log for compression level 1 with the largest source size.
         */
        if (pledgedSrcSize != ZSTD144_CONTENTSIZE_UNKNOWN) {
            U32 const limitedSrcSize = (U32)MIN(pledgedSrcSize, 1U << 19);
            U32 const limitedSrcLog = limitedSrcSize > 1 ? ZSTD144_highbit32(limitedSrcSize - 1) + 1 : 1;
            params.cParams.windowLog = MAX(params.cParams.windowLog, limitedSrcLog);
        }
        params.fParams = fParams;
        return ZSTD144_compressBegin_internal(cctx,
                                           NULL, 0, ZSTD144_dct_auto, ZSTD144_dtlm_fast,
                                           cdict,
                                           &params, pledgedSrcSize,
                                           ZSTDb_not_buffered);
    }
}

/* ZSTD144_compressBegin_usingCDict() :
 * pledgedSrcSize=0 means "unknown"
 * if pledgedSrcSize>0, it will enable contentSizeFlag */
size_t ZSTD144_compressBegin_usingCDict(ZSTD144_CCtx* cctx, const ZSTD144_CDict* cdict)
{
    ZSTD144_frameParameters const fParams = { 0 /*content*/, 0 /*checksum*/, 0 /*noDictID*/ };
    DEBUGLOG(4, "ZSTD144_compressBegin_usingCDict : dictIDFlag == %u", !fParams.noDictIDFlag);
    return ZSTD144_compressBegin_usingCDict_advanced(cctx, cdict, fParams, ZSTD144_CONTENTSIZE_UNKNOWN);
}

size_t ZSTD144_compress_usingCDict_advanced(ZSTD144_CCtx* cctx,
                                void* dst, size_t dstCapacity,
                                const void* src, size_t srcSize,
                                const ZSTD144_CDict* cdict, ZSTD144_frameParameters fParams)
{
    FORWARD_IF_ERROR(ZSTD144_compressBegin_usingCDict_advanced(cctx, cdict, fParams, srcSize));   /* will check if cdict != NULL */
    return ZSTD144_compressEnd(cctx, dst, dstCapacity, src, srcSize);
}

/*! ZSTD144_compress_usingCDict() :
 *  Compression using a digested Dictionary.
 *  Faster startup than ZSTD144_compress_usingDict(), recommended when same dictionary is used multiple times.
 *  Note that compression parameters are decided at CDict creation time
 *  while frame parameters are hardcoded */
size_t ZSTD144_compress_usingCDict(ZSTD144_CCtx* cctx,
                                void* dst, size_t dstCapacity,
                                const void* src, size_t srcSize,
                                const ZSTD144_CDict* cdict)
{
    ZSTD144_frameParameters const fParams = { 1 /*content*/, 0 /*checksum*/, 0 /*noDictID*/ };
    return ZSTD144_compress_usingCDict_advanced(cctx, dst, dstCapacity, src, srcSize, cdict, fParams);
}



/* ******************************************************************
*  Streaming
********************************************************************/

ZSTD144_CStream* ZSTD144_createCStream(void)
{
    DEBUGLOG(3, "ZSTD144_createCStream");
    return ZSTD144_createCStream_advanced(ZSTD144_defaultCMem);
}

ZSTD144_CStream* ZSTD144_initStaticCStream(void *workspace, size_t workspaceSize)
{
    return ZSTD144_initStaticCCtx(workspace, workspaceSize);
}

ZSTD144_CStream* ZSTD144_createCStream_advanced(ZSTD144_customMem customMem)
{   /* CStream and CCtx are now same object */
    return ZSTD144_createCCtx_advanced(customMem);
}

size_t ZSTD144_freeCStream(ZSTD144_CStream* zcs)
{
    return ZSTD144_freeCCtx(zcs);   /* same object */
}



/*======   Initialization   ======*/

size_t ZSTD144_CStreamInSize(void)  { return ZSTD144_BLOCKSIZE_MAX; }

size_t ZSTD144_CStreamOutSize(void)
{
    return ZSTD144_compressBound(ZSTD144_BLOCKSIZE_MAX) + ZSTD144_blockHeaderSize + 4 /* 32-bits hash */ ;
}

static size_t ZSTD144_resetCStream_internal(ZSTD144_CStream* cctx,
                    const void* const dict, size_t const dictSize, ZSTD144_dictContentType_e const dictContentType,
                    const ZSTD144_CDict* const cdict,
                    ZSTD144_CCtx_params params, unsigned long long const pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_resetCStream_internal");
    /* Finalize the compression parameters */
    params.cParams = ZSTD144_getCParamsFromCCtxParams(&params, pledgedSrcSize, dictSize);
    /* params are supposed to be fully validated at this point */
    assert(!ZSTD144_isError(ZSTD144_checkCParams(params.cParams)));
    assert(!((dict) && (cdict)));  /* either dict or cdict, not both */

    FORWARD_IF_ERROR( ZSTD144_compressBegin_internal(cctx,
                                         dict, dictSize, dictContentType, ZSTD144_dtlm_fast,
                                         cdict,
                                         &params, pledgedSrcSize,
                                         ZSTDb_buffered) );

    cctx->inToCompress = 0;
    cctx->inBuffPos = 0;
    cctx->inBuffTarget = cctx->blockSize
                      + (cctx->blockSize == pledgedSrcSize);   /* for small input: avoid automatic flush on reaching end of block, since it would require to add a 3-bytes null block to end frame */
    cctx->outBuffContentSize = cctx->outBuffFlushedSize = 0;
    cctx->streamStage = zcss_load;
    cctx->frameEnded = 0;
    return 0;   /* ready to go */
}

/* ZSTD144_resetCStream():
 * pledgedSrcSize == 0 means "unknown" */
size_t ZSTD144_resetCStream(ZSTD144_CStream* zcs, unsigned long long pss)
{
    /* temporary : 0 interpreted as "unknown" during transition period.
     * Users willing to specify "unknown" **must** use ZSTD144_CONTENTSIZE_UNKNOWN.
     * 0 will be interpreted as "empty" in the future.
     */
    U64 const pledgedSrcSize = (pss==0) ? ZSTD144_CONTENTSIZE_UNKNOWN : pss;
    DEBUGLOG(4, "ZSTD144_resetCStream: pledgedSrcSize = %u", (unsigned)pledgedSrcSize);
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setPledgedSrcSize(zcs, pledgedSrcSize) );
    return 0;
}

/*! ZSTD144_initCStream_internal() :
 *  Note : for lib/compress only. Used by zstdmt_compress.c.
 *  Assumption 1 : params are valid
 *  Assumption 2 : either dict, or cdict, is defined, not both */
size_t ZSTD144_initCStream_internal(ZSTD144_CStream* zcs,
                    const void* dict, size_t dictSize, const ZSTD144_CDict* cdict,
                    const ZSTD144_CCtx_params* params,
                    unsigned long long pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_initCStream_internal");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setPledgedSrcSize(zcs, pledgedSrcSize) );
    assert(!ZSTD144_isError(ZSTD144_checkCParams(params->cParams)));
    zcs->requestedParams = *params;
    assert(!((dict) && (cdict)));  /* either dict or cdict, not both */
    if (dict) {
        FORWARD_IF_ERROR( ZSTD144_CCtx_loadDictionary(zcs, dict, dictSize) );
    } else {
        /* Dictionary is cleared if !cdict */
        FORWARD_IF_ERROR( ZSTD144_CCtx_refCDict(zcs, cdict) );
    }
    return 0;
}

/* ZSTD144_initCStream_usingCDict_advanced() :
 * same as ZSTD144_initCStream_usingCDict(), with control over frame parameters */
size_t ZSTD144_initCStream_usingCDict_advanced(ZSTD144_CStream* zcs,
                                            const ZSTD144_CDict* cdict,
                                            ZSTD144_frameParameters fParams,
                                            unsigned long long pledgedSrcSize)
{
    DEBUGLOG(4, "ZSTD144_initCStream_usingCDict_advanced");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setPledgedSrcSize(zcs, pledgedSrcSize) );
    zcs->requestedParams.fParams = fParams;
    FORWARD_IF_ERROR( ZSTD144_CCtx_refCDict(zcs, cdict) );
    return 0;
}

/* note : cdict must outlive compression session */
size_t ZSTD144_initCStream_usingCDict(ZSTD144_CStream* zcs, const ZSTD144_CDict* cdict)
{
    DEBUGLOG(4, "ZSTD144_initCStream_usingCDict");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_refCDict(zcs, cdict) );
    return 0;
}


/* ZSTD144_initCStream_advanced() :
 * pledgedSrcSize must be exact.
 * if srcSize is not known at init time, use value ZSTD144_CONTENTSIZE_UNKNOWN.
 * dict is loaded with default parameters ZSTD144_dct_auto and ZSTD144_dlm_byCopy. */
size_t ZSTD144_initCStream_advanced(ZSTD144_CStream* zcs,
                                 const void* dict, size_t dictSize,
                                 ZSTD144_parameters params, unsigned long long pss)
{
    /* for compatibility with older programs relying on this behavior.
     * Users should now specify ZSTD144_CONTENTSIZE_UNKNOWN.
     * This line will be removed in the future.
     */
    U64 const pledgedSrcSize = (pss==0 && params.fParams.contentSizeFlag==0) ? ZSTD144_CONTENTSIZE_UNKNOWN : pss;
    DEBUGLOG(4, "ZSTD144_initCStream_advanced");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setPledgedSrcSize(zcs, pledgedSrcSize) );
    FORWARD_IF_ERROR( ZSTD144_checkCParams(params.cParams) );
    zcs->requestedParams = ZSTD144_assignParamsToCCtxParams(&zcs->requestedParams, params);
    FORWARD_IF_ERROR( ZSTD144_CCtx_loadDictionary(zcs, dict, dictSize) );
    return 0;
}

size_t ZSTD144_initCStream_usingDict(ZSTD144_CStream* zcs, const void* dict, size_t dictSize, int compressionLevel)
{
    DEBUGLOG(4, "ZSTD144_initCStream_usingDict");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setParameter(zcs, ZSTD144_c_compressionLevel, compressionLevel) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_loadDictionary(zcs, dict, dictSize) );
    return 0;
}

size_t ZSTD144_initCStream_srcSize(ZSTD144_CStream* zcs, int compressionLevel, unsigned long long pss)
{
    /* temporary : 0 interpreted as "unknown" during transition period.
     * Users willing to specify "unknown" **must** use ZSTD144_CONTENTSIZE_UNKNOWN.
     * 0 will be interpreted as "empty" in the future.
     */
    U64 const pledgedSrcSize = (pss==0) ? ZSTD144_CONTENTSIZE_UNKNOWN : pss;
    DEBUGLOG(4, "ZSTD144_initCStream_srcSize");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_refCDict(zcs, NULL) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setParameter(zcs, ZSTD144_c_compressionLevel, compressionLevel) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setPledgedSrcSize(zcs, pledgedSrcSize) );
    return 0;
}

size_t ZSTD144_initCStream(ZSTD144_CStream* zcs, int compressionLevel)
{
    DEBUGLOG(4, "ZSTD144_initCStream");
    FORWARD_IF_ERROR( ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_refCDict(zcs, NULL) );
    FORWARD_IF_ERROR( ZSTD144_CCtx_setParameter(zcs, ZSTD144_c_compressionLevel, compressionLevel) );
    return 0;
}

/*======   Compression   ======*/

static size_t ZSTD144_nextInputSizeHint(const ZSTD144_CCtx* cctx)
{
    size_t hintInSize = cctx->inBuffTarget - cctx->inBuffPos;
    if (hintInSize==0) hintInSize = cctx->blockSize;
    return hintInSize;
}

static size_t ZSTD144_limitCopy(void* dst, size_t dstCapacity,
                       const void* src, size_t srcSize)
{
    size_t const length = MIN(dstCapacity, srcSize);
    if (length) memcpy(dst, src, length);
    return length;
}

/** ZSTD144_compressStream_generic():
 *  internal function for all *compressStream*() variants
 *  non-static, because can be called from zstdmt_compress.c
 * @return : hint size for next input */
static size_t ZSTD144_compressStream_generic(ZSTD144_CStream* zcs,
                                          ZSTD144_outBuffer* output,
                                          ZSTD144_inBuffer* input,
                                          ZSTD144_EndDirective const flushMode)
{
    const char* const istart = (const char*)input->src;
    const char* const iend = istart + input->size;
    const char* ip = istart + input->pos;
    char* const ostart = (char*)output->dst;
    char* const oend = ostart + output->size;
    char* op = ostart + output->pos;
    U32 someMoreWork = 1;

    /* check expectations */
    DEBUGLOG(5, "ZSTD144_compressStream_generic, flush=%u", (unsigned)flushMode);
    assert(zcs->inBuff != NULL);
    assert(zcs->inBuffSize > 0);
    assert(zcs->outBuff !=  NULL);
    assert(zcs->outBuffSize > 0);
    assert(output->pos <= output->size);
    assert(input->pos <= input->size);

    while (someMoreWork) {
        switch(zcs->streamStage)
        {
        case zcss_init:
            RETURN_ERROR(init_missing, "call ZSTD144_initCStream() first!");

        case zcss_load:
            if ( (flushMode == ZSTD144_e_end)
              && ((size_t)(oend-op) >= ZSTD144_compressBound(iend-ip))  /* enough dstCapacity */
              && (zcs->inBuffPos == 0) ) {
                /* shortcut to compression pass directly into output buffer */
                size_t const cSize = ZSTD144_compressEnd(zcs,
                                                op, oend-op, ip, iend-ip);
                DEBUGLOG(4, "ZSTD144_compressEnd : cSize=%u", (unsigned)cSize);
                FORWARD_IF_ERROR(cSize);
                ip = iend;
                op += cSize;
                zcs->frameEnded = 1;
                ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only);
                someMoreWork = 0; break;
            }
            /* complete loading into inBuffer */
            {   size_t const toLoad = zcs->inBuffTarget - zcs->inBuffPos;
                size_t const loaded = ZSTD144_limitCopy(
                                        zcs->inBuff + zcs->inBuffPos, toLoad,
                                        ip, iend-ip);
                zcs->inBuffPos += loaded;
                ip += loaded;
                if ( (flushMode == ZSTD144_e_continue)
                  && (zcs->inBuffPos < zcs->inBuffTarget) ) {
                    /* not enough input to fill full block : stop here */
                    someMoreWork = 0; break;
                }
                if ( (flushMode == ZSTD144_e_flush)
                  && (zcs->inBuffPos == zcs->inToCompress) ) {
                    /* empty */
                    someMoreWork = 0; break;
                }
            }
            /* compress current block (note : this stage cannot be stopped in the middle) */
            DEBUGLOG(5, "stream compression stage (flushMode==%u)", flushMode);
            {   void* cDst;
                size_t cSize;
                size_t const iSize = zcs->inBuffPos - zcs->inToCompress;
                size_t oSize = oend-op;
                unsigned const lastBlock = (flushMode == ZSTD144_e_end) && (ip==iend);
                if (oSize >= ZSTD144_compressBound(iSize))
                    cDst = op;   /* compress into output buffer, to skip flush stage */
                else
                    cDst = zcs->outBuff, oSize = zcs->outBuffSize;
                cSize = lastBlock ?
                        ZSTD144_compressEnd(zcs, cDst, oSize,
                                    zcs->inBuff + zcs->inToCompress, iSize) :
                        ZSTD144_compressContinue(zcs, cDst, oSize,
                                    zcs->inBuff + zcs->inToCompress, iSize);
                FORWARD_IF_ERROR(cSize);
                zcs->frameEnded = lastBlock;
                /* prepare next block */
                zcs->inBuffTarget = zcs->inBuffPos + zcs->blockSize;
                if (zcs->inBuffTarget > zcs->inBuffSize)
                    zcs->inBuffPos = 0, zcs->inBuffTarget = zcs->blockSize;
                DEBUGLOG(5, "inBuffTarget:%u / inBuffSize:%u",
                         (unsigned)zcs->inBuffTarget, (unsigned)zcs->inBuffSize);
                if (!lastBlock)
                    assert(zcs->inBuffTarget <= zcs->inBuffSize);
                zcs->inToCompress = zcs->inBuffPos;
                if (cDst == op) {  /* no need to flush */
                    op += cSize;
                    if (zcs->frameEnded) {
                        DEBUGLOG(5, "Frame completed directly in outBuffer");
                        someMoreWork = 0;
                        ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only);
                    }
                    break;
                }
                zcs->outBuffContentSize = cSize;
                zcs->outBuffFlushedSize = 0;
                zcs->streamStage = zcss_flush; /* pass-through to flush stage */
            }
	    /* fall-through */
        case zcss_flush:
            DEBUGLOG(5, "flush stage");
            {   size_t const toFlush = zcs->outBuffContentSize - zcs->outBuffFlushedSize;
                size_t const flushed = ZSTD144_limitCopy(op, (size_t)(oend-op),
                            zcs->outBuff + zcs->outBuffFlushedSize, toFlush);
                DEBUGLOG(5, "toFlush: %u into %u ==> flushed: %u",
                            (unsigned)toFlush, (unsigned)(oend-op), (unsigned)flushed);
                op += flushed;
                zcs->outBuffFlushedSize += flushed;
                if (toFlush!=flushed) {
                    /* flush not fully completed, presumably because dst is too small */
                    assert(op==oend);
                    someMoreWork = 0;
                    break;
                }
                zcs->outBuffContentSize = zcs->outBuffFlushedSize = 0;
                if (zcs->frameEnded) {
                    DEBUGLOG(5, "Frame completed on flush");
                    someMoreWork = 0;
                    ZSTD144_CCtx_reset(zcs, ZSTD144_reset_session_only);
                    break;
                }
                zcs->streamStage = zcss_load;
                break;
            }

        default: /* impossible */
            assert(0);
        }
    }

    input->pos = ip - istart;
    output->pos = op - ostart;
    if (zcs->frameEnded) return 0;
    return ZSTD144_nextInputSizeHint(zcs);
}

static size_t ZSTD144_nextInputSizeHint_MTorST(const ZSTD144_CCtx* cctx)
{
#ifdef ZSTD144_MULTITHREAD
    if (cctx->appliedParams.nbWorkers >= 1) {
        assert(cctx->mtctx != NULL);
        return ZSTDMT144_nextInputSizeHint(cctx->mtctx);
    }
#endif
    return ZSTD144_nextInputSizeHint(cctx);

}

size_t ZSTD144_compressStream(ZSTD144_CStream* zcs, ZSTD144_outBuffer* output, ZSTD144_inBuffer* input)
{
    FORWARD_IF_ERROR( ZSTD144_compressStream2(zcs, output, input, ZSTD144_e_continue) );
    return ZSTD144_nextInputSizeHint_MTorST(zcs);
}


size_t ZSTD144_compressStream2( ZSTD144_CCtx* cctx,
                             ZSTD144_outBuffer* output,
                             ZSTD144_inBuffer* input,
                             ZSTD144_EndDirective endOp)
{
    DEBUGLOG(5, "ZSTD144_compressStream2, endOp=%u ", (unsigned)endOp);
    /* check conditions */
    RETURN_ERROR_IF(output->pos > output->size, GENERIC);
    RETURN_ERROR_IF(input->pos  > input->size, GENERIC);
    assert(cctx!=NULL);

    /* transparent initialization stage */
    if (cctx->streamStage == zcss_init) {
        ZSTD144_CCtx_params params = cctx->requestedParams;
        ZSTD144_prefixDict const prefixDict = cctx->prefixDict;
        FORWARD_IF_ERROR( ZSTD144_initLocalDict(cctx) ); /* Init the local dict if present. */
        memset(&cctx->prefixDict, 0, sizeof(cctx->prefixDict));   /* single usage */
        assert(prefixDict.dict==NULL || cctx->cdict==NULL);    /* only one can be set */
        DEBUGLOG(4, "ZSTD144_compressStream2 : transparent init stage");
        if (endOp == ZSTD144_e_end) cctx->pledgedSrcSizePlusOne = input->size + 1;  /* auto-fix pledgedSrcSize */
        params.cParams = ZSTD144_getCParamsFromCCtxParams(
                &cctx->requestedParams, cctx->pledgedSrcSizePlusOne-1, 0 /*dictSize*/);


#ifdef ZSTD144_MULTITHREAD
        if ((cctx->pledgedSrcSizePlusOne-1) <= ZSTDMT144_JOBSIZE_MIN) {
            params.nbWorkers = 0; /* do not invoke multi-threading when src size is too small */
        }
        if (params.nbWorkers > 0) {
            /* mt context creation */
            if (cctx->mtctx == NULL) {
                DEBUGLOG(4, "ZSTD144_compressStream2: creating new mtctx for nbWorkers=%u",
                            params.nbWorkers);
                cctx->mtctx = ZSTDMT144_createCCtx_advanced((U32)params.nbWorkers, cctx->customMem);
                RETURN_ERROR_IF(cctx->mtctx == NULL, memory_allocation);
            }
            /* mt compression */
            DEBUGLOG(4, "call ZSTDMT144_initCStream_internal as nbWorkers=%u", params.nbWorkers);
            FORWARD_IF_ERROR( ZSTDMT144_initCStream_internal(
                        cctx->mtctx,
                        prefixDict.dict, prefixDict.dictSize, ZSTD144_dct_rawContent,
                        cctx->cdict, params, cctx->pledgedSrcSizePlusOne-1) );
            cctx->streamStage = zcss_load;
            cctx->appliedParams.nbWorkers = params.nbWorkers;
        } else
#endif
        {   FORWARD_IF_ERROR( ZSTD144_resetCStream_internal(cctx,
                            prefixDict.dict, prefixDict.dictSize, prefixDict.dictContentType,
                            cctx->cdict,
                            params, cctx->pledgedSrcSizePlusOne-1) );
            assert(cctx->streamStage == zcss_load);
            assert(cctx->appliedParams.nbWorkers == 0);
    }   }
    /* end of transparent initialization stage */

    /* compression stage */
#ifdef ZSTD144_MULTITHREAD
    if (cctx->appliedParams.nbWorkers > 0) {
        int const forceMaxProgress = (endOp == ZSTD144_e_flush || endOp == ZSTD144_e_end);
        size_t flushMin;
        assert(forceMaxProgress || endOp == ZSTD144_e_continue /* Protection for a new flush type */);
        if (cctx->cParamsChanged) {
            ZSTDMT144_updateCParams_whileCompressing(cctx->mtctx, &cctx->requestedParams);
            cctx->cParamsChanged = 0;
        }
        do {
            flushMin = ZSTDMT144_compressStream_generic(cctx->mtctx, output, input, endOp);
            if ( ZSTD144_isError(flushMin)
              || (endOp == ZSTD144_e_end && flushMin == 0) ) { /* compression completed */
                ZSTD144_CCtx_reset(cctx, ZSTD144_reset_session_only);
            }
            FORWARD_IF_ERROR(flushMin);
        } while (forceMaxProgress && flushMin != 0 && output->pos < output->size);
        DEBUGLOG(5, "completed ZSTD144_compressStream2 delegating to ZSTDMT144_compressStream_generic");
        /* Either we don't require maximum forward progress, we've finished the
         * flush, or we are out of output space.
         */
        assert(!forceMaxProgress || flushMin == 0 || output->pos == output->size);
        return flushMin;
    }
#endif
    FORWARD_IF_ERROR( ZSTD144_compressStream_generic(cctx, output, input, endOp) );
    DEBUGLOG(5, "completed ZSTD144_compressStream2");
    return cctx->outBuffContentSize - cctx->outBuffFlushedSize; /* remaining to flush */
}

size_t ZSTD144_compressStream2_simpleArgs (
                            ZSTD144_CCtx* cctx,
                            void* dst, size_t dstCapacity, size_t* dstPos,
                      const void* src, size_t srcSize, size_t* srcPos,
                            ZSTD144_EndDirective endOp)
{
    ZSTD144_outBuffer output = { dst, dstCapacity, *dstPos };
    ZSTD144_inBuffer  input  = { src, srcSize, *srcPos };
    /* ZSTD144_compressStream2() will check validity of dstPos and srcPos */
    size_t const cErr = ZSTD144_compressStream2(cctx, &output, &input, endOp);
    *dstPos = output.pos;
    *srcPos = input.pos;
    return cErr;
}

size_t ZSTD144_compress2(ZSTD144_CCtx* cctx,
                      void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{
    ZSTD144_CCtx_reset(cctx, ZSTD144_reset_session_only);
    {   size_t oPos = 0;
        size_t iPos = 0;
        size_t const result = ZSTD144_compressStream2_simpleArgs(cctx,
                                        dst, dstCapacity, &oPos,
                                        src, srcSize, &iPos,
                                        ZSTD144_e_end);
        FORWARD_IF_ERROR(result);
        if (result != 0) {  /* compression not completed, due to lack of output space */
            assert(oPos == dstCapacity);
            RETURN_ERROR(dstSize_tooSmall);
        }
        assert(iPos == srcSize);   /* all input is expected consumed */
        return oPos;
    }
}

/*======   Finalize   ======*/

/*! ZSTD144_flushStream() :
 * @return : amount of data remaining to flush */
size_t ZSTD144_flushStream(ZSTD144_CStream* zcs, ZSTD144_outBuffer* output)
{
    ZSTD144_inBuffer input = { NULL, 0, 0 };
    return ZSTD144_compressStream2(zcs, output, &input, ZSTD144_e_flush);
}


size_t ZSTD144_endStream(ZSTD144_CStream* zcs, ZSTD144_outBuffer* output)
{
    ZSTD144_inBuffer input = { NULL, 0, 0 };
    size_t const remainingToFlush = ZSTD144_compressStream2(zcs, output, &input, ZSTD144_e_end);
    FORWARD_IF_ERROR( remainingToFlush );
    if (zcs->appliedParams.nbWorkers > 0) return remainingToFlush;   /* minimal estimation */
    /* single thread mode : attempt to calculate remaining to flush more precisely */
    {   size_t const lastBlockSize = zcs->frameEnded ? 0 : ZSTD144_BLOCKHEADERSIZE;
        size_t const checksumSize = (size_t)(zcs->frameEnded ? 0 : zcs->appliedParams.fParams.checksumFlag * 4);
        size_t const toFlush = remainingToFlush + lastBlockSize + checksumSize;
        DEBUGLOG(4, "ZSTD144_endStream : remaining to flush : %u", (unsigned)toFlush);
        return toFlush;
    }
}


/*-=====  Pre-defined compression levels  =====-*/

#define ZSTD144_MAX_CLEVEL     22
int ZSTD144_maxCLevel(void) { return ZSTD144_MAX_CLEVEL; }
int ZSTD144_minCLevel(void) { return (int)-ZSTD144_TARGETLENGTH_MAX; }

static const ZSTD144_compressionParameters ZSTD144_defaultCParameters[4][ZSTD144_MAX_CLEVEL+1] = {
{   /* "default" - for any srcSize > 256 KB */
    /* W,  C,  H,  S,  L, TL, strat */
    { 19, 12, 13,  1,  6,  1, ZSTD144_fast    },  /* base for negative levels */
    { 19, 13, 14,  1,  7,  0, ZSTD144_fast    },  /* level  1 */
    { 20, 15, 16,  1,  6,  0, ZSTD144_fast    },  /* level  2 */
    { 21, 16, 17,  1,  5,  0, ZSTD144_dfast   },  /* level  3 */
    { 21, 18, 18,  1,  5,  0, ZSTD144_dfast   },  /* level  4 */
    { 21, 18, 19,  2,  5,  2, ZSTD144_greedy  },  /* level  5 */
    { 21, 19, 19,  3,  5,  4, ZSTD144_greedy  },  /* level  6 */
    { 21, 19, 19,  3,  5,  8, ZSTD144_lazy    },  /* level  7 */
    { 21, 19, 19,  3,  5, 16, ZSTD144_lazy2   },  /* level  8 */
    { 21, 19, 20,  4,  5, 16, ZSTD144_lazy2   },  /* level  9 */
    { 22, 20, 21,  4,  5, 16, ZSTD144_lazy2   },  /* level 10 */
    { 22, 21, 22,  4,  5, 16, ZSTD144_lazy2   },  /* level 11 */
    { 22, 21, 22,  5,  5, 16, ZSTD144_lazy2   },  /* level 12 */
    { 22, 21, 22,  5,  5, 32, ZSTD144_btlazy2 },  /* level 13 */
    { 22, 22, 23,  5,  5, 32, ZSTD144_btlazy2 },  /* level 14 */
    { 22, 23, 23,  6,  5, 32, ZSTD144_btlazy2 },  /* level 15 */
    { 22, 22, 22,  5,  5, 48, ZSTD144_btopt   },  /* level 16 */
    { 23, 23, 22,  5,  4, 64, ZSTD144_btopt   },  /* level 17 */
    { 23, 23, 22,  6,  3, 64, ZSTD144_btultra },  /* level 18 */
    { 23, 24, 22,  7,  3,256, ZSTD144_btultra2},  /* level 19 */
    { 25, 25, 23,  7,  3,256, ZSTD144_btultra2},  /* level 20 */
    { 26, 26, 24,  7,  3,512, ZSTD144_btultra2},  /* level 21 */
    { 27, 27, 25,  9,  3,999, ZSTD144_btultra2},  /* level 22 */
},
{   /* for srcSize <= 256 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 18, 12, 13,  1,  5,  1, ZSTD144_fast    },  /* base for negative levels */
    { 18, 13, 14,  1,  6,  0, ZSTD144_fast    },  /* level  1 */
    { 18, 14, 14,  1,  5,  0, ZSTD144_dfast   },  /* level  2 */
    { 18, 16, 16,  1,  4,  0, ZSTD144_dfast   },  /* level  3 */
    { 18, 16, 17,  2,  5,  2, ZSTD144_greedy  },  /* level  4.*/
    { 18, 18, 18,  3,  5,  2, ZSTD144_greedy  },  /* level  5.*/
    { 18, 18, 19,  3,  5,  4, ZSTD144_lazy    },  /* level  6.*/
    { 18, 18, 19,  4,  4,  4, ZSTD144_lazy    },  /* level  7 */
    { 18, 18, 19,  4,  4,  8, ZSTD144_lazy2   },  /* level  8 */
    { 18, 18, 19,  5,  4,  8, ZSTD144_lazy2   },  /* level  9 */
    { 18, 18, 19,  6,  4,  8, ZSTD144_lazy2   },  /* level 10 */
    { 18, 18, 19,  5,  4, 12, ZSTD144_btlazy2 },  /* level 11.*/
    { 18, 19, 19,  7,  4, 12, ZSTD144_btlazy2 },  /* level 12.*/
    { 18, 18, 19,  4,  4, 16, ZSTD144_btopt   },  /* level 13 */
    { 18, 18, 19,  4,  3, 32, ZSTD144_btopt   },  /* level 14.*/
    { 18, 18, 19,  6,  3,128, ZSTD144_btopt   },  /* level 15.*/
    { 18, 19, 19,  6,  3,128, ZSTD144_btultra },  /* level 16.*/
    { 18, 19, 19,  8,  3,256, ZSTD144_btultra },  /* level 17.*/
    { 18, 19, 19,  6,  3,128, ZSTD144_btultra2},  /* level 18.*/
    { 18, 19, 19,  8,  3,256, ZSTD144_btultra2},  /* level 19.*/
    { 18, 19, 19, 10,  3,512, ZSTD144_btultra2},  /* level 20.*/
    { 18, 19, 19, 12,  3,512, ZSTD144_btultra2},  /* level 21.*/
    { 18, 19, 19, 13,  3,999, ZSTD144_btultra2},  /* level 22.*/
},
{   /* for srcSize <= 128 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 17, 12, 12,  1,  5,  1, ZSTD144_fast    },  /* base for negative levels */
    { 17, 12, 13,  1,  6,  0, ZSTD144_fast    },  /* level  1 */
    { 17, 13, 15,  1,  5,  0, ZSTD144_fast    },  /* level  2 */
    { 17, 15, 16,  2,  5,  0, ZSTD144_dfast   },  /* level  3 */
    { 17, 17, 17,  2,  4,  0, ZSTD144_dfast   },  /* level  4 */
    { 17, 16, 17,  3,  4,  2, ZSTD144_greedy  },  /* level  5 */
    { 17, 17, 17,  3,  4,  4, ZSTD144_lazy    },  /* level  6 */
    { 17, 17, 17,  3,  4,  8, ZSTD144_lazy2   },  /* level  7 */
    { 17, 17, 17,  4,  4,  8, ZSTD144_lazy2   },  /* level  8 */
    { 17, 17, 17,  5,  4,  8, ZSTD144_lazy2   },  /* level  9 */
    { 17, 17, 17,  6,  4,  8, ZSTD144_lazy2   },  /* level 10 */
    { 17, 17, 17,  5,  4,  8, ZSTD144_btlazy2 },  /* level 11 */
    { 17, 18, 17,  7,  4, 12, ZSTD144_btlazy2 },  /* level 12 */
    { 17, 18, 17,  3,  4, 12, ZSTD144_btopt   },  /* level 13.*/
    { 17, 18, 17,  4,  3, 32, ZSTD144_btopt   },  /* level 14.*/
    { 17, 18, 17,  6,  3,256, ZSTD144_btopt   },  /* level 15.*/
    { 17, 18, 17,  6,  3,128, ZSTD144_btultra },  /* level 16.*/
    { 17, 18, 17,  8,  3,256, ZSTD144_btultra },  /* level 17.*/
    { 17, 18, 17, 10,  3,512, ZSTD144_btultra },  /* level 18.*/
    { 17, 18, 17,  5,  3,256, ZSTD144_btultra2},  /* level 19.*/
    { 17, 18, 17,  7,  3,512, ZSTD144_btultra2},  /* level 20.*/
    { 17, 18, 17,  9,  3,512, ZSTD144_btultra2},  /* level 21.*/
    { 17, 18, 17, 11,  3,999, ZSTD144_btultra2},  /* level 22.*/
},
{   /* for srcSize <= 16 KB */
    /* W,  C,  H,  S,  L,  T, strat */
    { 14, 12, 13,  1,  5,  1, ZSTD144_fast    },  /* base for negative levels */
    { 14, 14, 15,  1,  5,  0, ZSTD144_fast    },  /* level  1 */
    { 14, 14, 15,  1,  4,  0, ZSTD144_fast    },  /* level  2 */
    { 14, 14, 15,  2,  4,  0, ZSTD144_dfast   },  /* level  3 */
    { 14, 14, 14,  4,  4,  2, ZSTD144_greedy  },  /* level  4 */
    { 14, 14, 14,  3,  4,  4, ZSTD144_lazy    },  /* level  5.*/
    { 14, 14, 14,  4,  4,  8, ZSTD144_lazy2   },  /* level  6 */
    { 14, 14, 14,  6,  4,  8, ZSTD144_lazy2   },  /* level  7 */
    { 14, 14, 14,  8,  4,  8, ZSTD144_lazy2   },  /* level  8.*/
    { 14, 15, 14,  5,  4,  8, ZSTD144_btlazy2 },  /* level  9.*/
    { 14, 15, 14,  9,  4,  8, ZSTD144_btlazy2 },  /* level 10.*/
    { 14, 15, 14,  3,  4, 12, ZSTD144_btopt   },  /* level 11.*/
    { 14, 15, 14,  4,  3, 24, ZSTD144_btopt   },  /* level 12.*/
    { 14, 15, 14,  5,  3, 32, ZSTD144_btultra },  /* level 13.*/
    { 14, 15, 15,  6,  3, 64, ZSTD144_btultra },  /* level 14.*/
    { 14, 15, 15,  7,  3,256, ZSTD144_btultra },  /* level 15.*/
    { 14, 15, 15,  5,  3, 48, ZSTD144_btultra2},  /* level 16.*/
    { 14, 15, 15,  6,  3,128, ZSTD144_btultra2},  /* level 17.*/
    { 14, 15, 15,  7,  3,256, ZSTD144_btultra2},  /* level 18.*/
    { 14, 15, 15,  8,  3,256, ZSTD144_btultra2},  /* level 19.*/
    { 14, 15, 15,  8,  3,512, ZSTD144_btultra2},  /* level 20.*/
    { 14, 15, 15,  9,  3,512, ZSTD144_btultra2},  /* level 21.*/
    { 14, 15, 15, 10,  3,999, ZSTD144_btultra2},  /* level 22.*/
},
};

/*! ZSTD144_getCParams() :
 * @return ZSTD144_compressionParameters structure for a selected compression level, srcSize and dictSize.
 *  Size values are optional, provide 0 if not known or unused */
ZSTD144_compressionParameters ZSTD144_getCParams(int compressionLevel, unsigned long long srcSizeHint, size_t dictSize)
{
    size_t const addedSize = srcSizeHint ? 0 : 500;
    U64 const rSize = srcSizeHint+dictSize ? srcSizeHint+dictSize+addedSize : ZSTD144_CONTENTSIZE_UNKNOWN;  /* intentional overflow for srcSizeHint == ZSTD144_CONTENTSIZE_UNKNOWN */
    U32 const tableID = (rSize <= 256 KB) + (rSize <= 128 KB) + (rSize <= 16 KB);
    int row = compressionLevel;
    DEBUGLOG(5, "ZSTD144_getCParams (cLevel=%i)", compressionLevel);
    if (compressionLevel == 0) row = ZSTD144_CLEVEL_DEFAULT;   /* 0 == default */
    if (compressionLevel < 0) row = 0;   /* entry 0 is baseline for fast mode */
    if (compressionLevel > ZSTD144_MAX_CLEVEL) row = ZSTD144_MAX_CLEVEL;
    {   ZSTD144_compressionParameters cp = ZSTD144_defaultCParameters[tableID][row];
        if (compressionLevel < 0) cp.targetLength = (unsigned)(-compressionLevel);   /* acceleration factor */
        return ZSTD144_adjustCParams_internal(cp, srcSizeHint, dictSize);               /* refine parameters based on srcSize & dictSize */
    }
}

/*! ZSTD144_getParams() :
 *  same idea as ZSTD144_getCParams()
 * @return a `ZSTD144_parameters` structure (instead of `ZSTD144_compressionParameters`).
 *  Fields of `ZSTD144_frameParameters` are set to default values */
ZSTD144_parameters ZSTD144_getParams(int compressionLevel, unsigned long long srcSizeHint, size_t dictSize) {
    ZSTD144_parameters params;
    ZSTD144_compressionParameters const cParams = ZSTD144_getCParams(compressionLevel, srcSizeHint, dictSize);
    DEBUGLOG(5, "ZSTD144_getParams (cLevel=%i)", compressionLevel);
    memset(&params, 0, sizeof(params));
    params.cParams = cParams;
    params.fParams.contentSizeFlag = 1;
    return params;
}
