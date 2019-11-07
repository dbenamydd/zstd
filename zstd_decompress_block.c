/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* zstd_decompress_block :
 * this module takes care of decompressing _compressed_ block */

/*-*******************************************************
*  Dependencies
*********************************************************/
#include <string.h>      /* memcpy, memmove, memset */
#include "compiler.h"    /* prefetch */
#include "cpu.h"         /* bmi2 */
#include "mem.h"         /* low level memory routines */
#define FSE144_STATIC_LINKING_ONLY
#include "fse.h"
#define HUF144_STATIC_LINKING_ONLY
#include "huf.h"
#include "zstd_internal.h"
#include "zstd_decompress_internal.h"   /* ZSTD144_DCtx */
#include "zstd_ddict.h"  /* ZSTD144_DDictDictContent */
#include "zstd_decompress_block.h"

/*_*******************************************************
*  Macros
**********************************************************/

/* These two optional macros force the use one way or another of the two
 * ZSTD144_decompressSequences implementations. You can't force in both directions
 * at the same time.
 */
#if defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG)
#error "Cannot force the use of the short and the long ZSTD144_decompressSequences variants!"
#endif


/*_*******************************************************
*  Memory operations
**********************************************************/
static void ZSTD144_copy4(void* dst, const void* src) { memcpy(dst, src, 4); }


/*-*************************************************************
 *   Block decoding
 ***************************************************************/

/*! ZSTD144_getcBlockSize() :
 *  Provides the size of compressed block from block header `src` */
size_t ZSTD144_getcBlockSize(const void* src, size_t srcSize,
                          blockProperties_t* bpPtr)
{
    RETURN_ERROR_IF(srcSize < ZSTD144_blockHeaderSize, srcSize_wrong);

    {   U32 const cBlockHeader = MEM_readLE24(src);
        U32 const cSize = cBlockHeader >> 3;
        bpPtr->lastBlock = cBlockHeader & 1;
        bpPtr->blockType = (blockType_e)((cBlockHeader >> 1) & 3);
        bpPtr->origSize = cSize;   /* only useful for RLE */
        if (bpPtr->blockType == bt_rle) return 1;
        RETURN_ERROR_IF(bpPtr->blockType == bt_reserved, corruption_detected);
        return cSize;
    }
}


/* Hidden declaration for fullbench */
size_t ZSTD144_decodeLiteralsBlock(ZSTD144_DCtx* dctx,
                          const void* src, size_t srcSize);
/*! ZSTD144_decodeLiteralsBlock() :
 * @return : nb of bytes read from src (< srcSize )
 *  note : symbol not declared but exposed for fullbench */
size_t ZSTD144_decodeLiteralsBlock(ZSTD144_DCtx* dctx,
                          const void* src, size_t srcSize)   /* note : srcSize < BLOCKSIZE */
{
    DEBUGLOG(5, "ZSTD144_decodeLiteralsBlock");
    RETURN_ERROR_IF(srcSize < MIN_CBLOCK_SIZE, corruption_detected);

    {   const BYTE* const istart = (const BYTE*) src;
        symbolEncodingType_e const litEncType = (symbolEncodingType_e)(istart[0] & 3);

        switch(litEncType)
        {
        case set_repeat:
            DEBUGLOG(5, "set_repeat flag : re-using stats from previous compressed literals block");
            RETURN_ERROR_IF(dctx->litEntropy==0, dictionary_corrupted);
            /* fall-through */

        case set_compressed:
            RETURN_ERROR_IF(srcSize < 5, corruption_detected, "srcSize >= MIN_CBLOCK_SIZE == 3; here we need up to 5 for case 3");
            {   size_t lhSize, litSize, litCSize;
                U32 singleStream=0;
                U32 const lhlCode = (istart[0] >> 2) & 3;
                U32 const lhc = MEM_readLE32(istart);
                size_t hufSuccess;
                switch(lhlCode)
                {
                case 0: case 1: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    /* 2 - 2 - 10 - 10 */
                    singleStream = !lhlCode;
                    lhSize = 3;
                    litSize  = (lhc >> 4) & 0x3FF;
                    litCSize = (lhc >> 14) & 0x3FF;
                    break;
                case 2:
                    /* 2 - 2 - 14 - 14 */
                    lhSize = 4;
                    litSize  = (lhc >> 4) & 0x3FFF;
                    litCSize = lhc >> 18;
                    break;
                case 3:
                    /* 2 - 2 - 18 - 18 */
                    lhSize = 5;
                    litSize  = (lhc >> 4) & 0x3FFFF;
                    litCSize = (lhc >> 22) + ((size_t)istart[4] << 10);
                    break;
                }
                RETURN_ERROR_IF(litSize > ZSTD144_BLOCKSIZE_MAX, corruption_detected);
                RETURN_ERROR_IF(litCSize + lhSize > srcSize, corruption_detected);

                /* prefetch huffman table if cold */
                if (dctx->ddictIsCold && (litSize > 768 /* heuristic */)) {
                    PREFETCH_AREA(dctx->HUFptr, sizeof(dctx->entropy.hufTable));
                }

                if (litEncType==set_repeat) {
                    if (singleStream) {
                        hufSuccess = HUF144_decompress1X_usingDTable_bmi2(
                            dctx->litBuffer, litSize, istart+lhSize, litCSize,
                            dctx->HUFptr, dctx->bmi2);
                    } else {
                        hufSuccess = HUF144_decompress4X_usingDTable_bmi2(
                            dctx->litBuffer, litSize, istart+lhSize, litCSize,
                            dctx->HUFptr, dctx->bmi2);
                    }
                } else {
                    if (singleStream) {
#if defined(HUF144_FORCE_DECOMPRESS_X2)
                        hufSuccess = HUF144_decompress1X_DCtx_wksp(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace));
#else
                        hufSuccess = HUF144_decompress1X1_DCtx_wksp_bmi2(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace), dctx->bmi2);
#endif
                    } else {
                        hufSuccess = HUF144_decompress4X_hufOnly_wksp_bmi2(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace), dctx->bmi2);
                    }
                }

                RETURN_ERROR_IF(HUF144_isError(hufSuccess), corruption_detected);

                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                dctx->litEntropy = 1;
                if (litEncType==set_compressed) dctx->HUFptr = dctx->entropy.hufTable;
                memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                return litCSize + lhSize;
            }

        case set_basic:
            {   size_t litSize, lhSize;
                U32 const lhlCode = ((istart[0]) >> 2) & 3;
                switch(lhlCode)
                {
                case 0: case 2: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    lhSize = 1;
                    litSize = istart[0] >> 3;
                    break;
                case 1:
                    lhSize = 2;
                    litSize = MEM_readLE16(istart) >> 4;
                    break;
                case 3:
                    lhSize = 3;
                    litSize = MEM_readLE24(istart) >> 4;
                    break;
                }

                if (lhSize+litSize+WILDCOPY_OVERLENGTH > srcSize) {  /* risk reading beyond src buffer with wildcopy */
                    RETURN_ERROR_IF(litSize+lhSize > srcSize, corruption_detected);
                    memcpy(dctx->litBuffer, istart+lhSize, litSize);
                    dctx->litPtr = dctx->litBuffer;
                    dctx->litSize = litSize;
                    memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                    return lhSize+litSize;
                }
                /* direct reference into compressed stream */
                dctx->litPtr = istart+lhSize;
                dctx->litSize = litSize;
                return lhSize+litSize;
            }

        case set_rle:
            {   U32 const lhlCode = ((istart[0]) >> 2) & 3;
                size_t litSize, lhSize;
                switch(lhlCode)
                {
                case 0: case 2: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    lhSize = 1;
                    litSize = istart[0] >> 3;
                    break;
                case 1:
                    lhSize = 2;
                    litSize = MEM_readLE16(istart) >> 4;
                    break;
                case 3:
                    lhSize = 3;
                    litSize = MEM_readLE24(istart) >> 4;
                    RETURN_ERROR_IF(srcSize<4, corruption_detected, "srcSize >= MIN_CBLOCK_SIZE == 3; here we need lhSize+1 = 4");
                    break;
                }
                RETURN_ERROR_IF(litSize > ZSTD144_BLOCKSIZE_MAX, corruption_detected);
                memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                return lhSize+1;
            }
        default:
            RETURN_ERROR(corruption_detected, "impossible");
        }
    }
}

/* Default FSE distribution tables.
 * These are pre-calculated FSE decoding tables using default distributions as defined in specification :
 * https://github.com/facebook/zstd/blob/master/doc/zstd_compression_format.md#default-distributions
 * They were generated programmatically with following method :
 * - start from default distributions, present in /lib/common/zstd_internal.h
 * - generate tables normally, using ZSTD144_buildFSETable()
 * - printout the content of tables
 * - pretify output, report below, test with fuzzer to ensure it's correct */

/* Default FSE distribution table for Literal Lengths */
static const ZSTD144_seqSymbol LL144_defaultDTable[(1<<LL144_DEFAULTNORMLOG)+1] = {
     {  1,  1,  1, LL144_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
     /* nextState, nbAddBits, nbBits, baseVal */
     {  0,  0,  4,    0},  { 16,  0,  4,    0},
     { 32,  0,  5,    1},  {  0,  0,  5,    3},
     {  0,  0,  5,    4},  {  0,  0,  5,    6},
     {  0,  0,  5,    7},  {  0,  0,  5,    9},
     {  0,  0,  5,   10},  {  0,  0,  5,   12},
     {  0,  0,  6,   14},  {  0,  1,  5,   16},
     {  0,  1,  5,   20},  {  0,  1,  5,   22},
     {  0,  2,  5,   28},  {  0,  3,  5,   32},
     {  0,  4,  5,   48},  { 32,  6,  5,   64},
     {  0,  7,  5,  128},  {  0,  8,  6,  256},
     {  0, 10,  6, 1024},  {  0, 12,  6, 4096},
     { 32,  0,  4,    0},  {  0,  0,  4,    1},
     {  0,  0,  5,    2},  { 32,  0,  5,    4},
     {  0,  0,  5,    5},  { 32,  0,  5,    7},
     {  0,  0,  5,    8},  { 32,  0,  5,   10},
     {  0,  0,  5,   11},  {  0,  0,  6,   13},
     { 32,  1,  5,   16},  {  0,  1,  5,   18},
     { 32,  1,  5,   22},  {  0,  2,  5,   24},
     { 32,  3,  5,   32},  {  0,  3,  5,   40},
     {  0,  6,  4,   64},  { 16,  6,  4,   64},
     { 32,  7,  5,  128},  {  0,  9,  6,  512},
     {  0, 11,  6, 2048},  { 48,  0,  4,    0},
     { 16,  0,  4,    1},  { 32,  0,  5,    2},
     { 32,  0,  5,    3},  { 32,  0,  5,    5},
     { 32,  0,  5,    6},  { 32,  0,  5,    8},
     { 32,  0,  5,    9},  { 32,  0,  5,   11},
     { 32,  0,  5,   12},  {  0,  0,  6,   15},
     { 32,  1,  5,   18},  { 32,  1,  5,   20},
     { 32,  2,  5,   24},  { 32,  2,  5,   28},
     { 32,  3,  5,   40},  { 32,  4,  5,   48},
     {  0, 16,  6,65536},  {  0, 15,  6,32768},
     {  0, 14,  6,16384},  {  0, 13,  6, 8192},
};   /* LL144_defaultDTable */

/* Default FSE distribution table for Offset Codes */
static const ZSTD144_seqSymbol OF144_defaultDTable[(1<<OF144_DEFAULTNORMLOG)+1] = {
    {  1,  1,  1, OF144_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
    /* nextState, nbAddBits, nbBits, baseVal */
    {  0,  0,  5,    0},     {  0,  6,  4,   61},
    {  0,  9,  5,  509},     {  0, 15,  5,32765},
    {  0, 21,  5,2097149},   {  0,  3,  5,    5},
    {  0,  7,  4,  125},     {  0, 12,  5, 4093},
    {  0, 18,  5,262141},    {  0, 23,  5,8388605},
    {  0,  5,  5,   29},     {  0,  8,  4,  253},
    {  0, 14,  5,16381},     {  0, 20,  5,1048573},
    {  0,  2,  5,    1},     { 16,  7,  4,  125},
    {  0, 11,  5, 2045},     {  0, 17,  5,131069},
    {  0, 22,  5,4194301},   {  0,  4,  5,   13},
    { 16,  8,  4,  253},     {  0, 13,  5, 8189},
    {  0, 19,  5,524285},    {  0,  1,  5,    1},
    { 16,  6,  4,   61},     {  0, 10,  5, 1021},
    {  0, 16,  5,65533},     {  0, 28,  5,268435453},
    {  0, 27,  5,134217725}, {  0, 26,  5,67108861},
    {  0, 25,  5,33554429},  {  0, 24,  5,16777213},
};   /* OF144_defaultDTable */


/* Default FSE distribution table for Match Lengths */
static const ZSTD144_seqSymbol ML144_defaultDTable[(1<<ML144_DEFAULTNORMLOG)+1] = {
    {  1,  1,  1, ML144_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
    /* nextState, nbAddBits, nbBits, baseVal */
    {  0,  0,  6,    3},  {  0,  0,  4,    4},
    { 32,  0,  5,    5},  {  0,  0,  5,    6},
    {  0,  0,  5,    8},  {  0,  0,  5,    9},
    {  0,  0,  5,   11},  {  0,  0,  6,   13},
    {  0,  0,  6,   16},  {  0,  0,  6,   19},
    {  0,  0,  6,   22},  {  0,  0,  6,   25},
    {  0,  0,  6,   28},  {  0,  0,  6,   31},
    {  0,  0,  6,   34},  {  0,  1,  6,   37},
    {  0,  1,  6,   41},  {  0,  2,  6,   47},
    {  0,  3,  6,   59},  {  0,  4,  6,   83},
    {  0,  7,  6,  131},  {  0,  9,  6,  515},
    { 16,  0,  4,    4},  {  0,  0,  4,    5},
    { 32,  0,  5,    6},  {  0,  0,  5,    7},
    { 32,  0,  5,    9},  {  0,  0,  5,   10},
    {  0,  0,  6,   12},  {  0,  0,  6,   15},
    {  0,  0,  6,   18},  {  0,  0,  6,   21},
    {  0,  0,  6,   24},  {  0,  0,  6,   27},
    {  0,  0,  6,   30},  {  0,  0,  6,   33},
    {  0,  1,  6,   35},  {  0,  1,  6,   39},
    {  0,  2,  6,   43},  {  0,  3,  6,   51},
    {  0,  4,  6,   67},  {  0,  5,  6,   99},
    {  0,  8,  6,  259},  { 32,  0,  4,    4},
    { 48,  0,  4,    4},  { 16,  0,  4,    5},
    { 32,  0,  5,    7},  { 32,  0,  5,    8},
    { 32,  0,  5,   10},  { 32,  0,  5,   11},
    {  0,  0,  6,   14},  {  0,  0,  6,   17},
    {  0,  0,  6,   20},  {  0,  0,  6,   23},
    {  0,  0,  6,   26},  {  0,  0,  6,   29},
    {  0,  0,  6,   32},  {  0, 16,  6,65539},
    {  0, 15,  6,32771},  {  0, 14,  6,16387},
    {  0, 13,  6, 8195},  {  0, 12,  6, 4099},
    {  0, 11,  6, 2051},  {  0, 10,  6, 1027},
};   /* ML144_defaultDTable */


static void ZSTD144_buildSeqTable_rle(ZSTD144_seqSymbol* dt, U32 baseValue, U32 nbAddBits)
{
    void* ptr = dt;
    ZSTD144_seqSymbol_header* const DTableH = (ZSTD144_seqSymbol_header*)ptr;
    ZSTD144_seqSymbol* const cell = dt + 1;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->nbBits = 0;
    cell->nextState = 0;
    assert(nbAddBits < 255);
    cell->nbAdditionalBits = (BYTE)nbAddBits;
    cell->baseValue = baseValue;
}


/* ZSTD144_buildFSETable() :
 * generate FSE decoding table for one symbol (ll, ml or off)
 * cannot fail if input is valid =>
 * all inputs are presumed validated at this stage */
void
ZSTD144_buildFSETable(ZSTD144_seqSymbol* dt,
            const short* normalizedCounter, unsigned maxSymbolValue,
            const U32* baseValue, const U32* nbAdditionalBits,
            unsigned tableLog)
{
    ZSTD144_seqSymbol* const tableDecode = dt+1;
    U16 symbolNext[MaxSeq+1];

    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;
    U32 highThreshold = tableSize-1;

    /* Sanity Checks */
    assert(maxSymbolValue <= MaxSeq);
    assert(tableLog <= MaxFSELog);

    /* Init, lay down lowprob symbols */
    {   ZSTD144_seqSymbol_header DTableH;
        DTableH.tableLog = tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].baseValue = s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    assert(normalizedCounter[s]>=0);
                    symbolNext[s] = (U16)normalizedCounter[s];
        }   }   }
        memcpy(dt, &DTableH, sizeof(DTableH));
    }

    /* Spread symbols */
    {   U32 const tableMask = tableSize-1;
        U32 const step = FSE144_TABLESTEP(tableSize);
        U32 s, position = 0;
        for (s=0; s<maxSV1; s++) {
            int i;
            for (i=0; i<normalizedCounter[s]; i++) {
                tableDecode[position].baseValue = s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }
        assert(position == 0); /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {   U32 u;
        for (u=0; u<tableSize; u++) {
            U32 const symbol = tableDecode[u].baseValue;
            U32 const nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - BIT144_highbit32(nextState) );
            tableDecode[u].nextState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
            assert(nbAdditionalBits[symbol] < 255);
            tableDecode[u].nbAdditionalBits = (BYTE)nbAdditionalBits[symbol];
            tableDecode[u].baseValue = baseValue[symbol];
    }   }
}


/*! ZSTD144_buildSeqTable() :
 * @return : nb bytes read from src,
 *           or an error code if it fails */
static size_t ZSTD144_buildSeqTable(ZSTD144_seqSymbol* DTableSpace, const ZSTD144_seqSymbol** DTablePtr,
                                 symbolEncodingType_e type, unsigned max, U32 maxLog,
                                 const void* src, size_t srcSize,
                                 const U32* baseValue, const U32* nbAdditionalBits,
                                 const ZSTD144_seqSymbol* defaultTable, U32 flagRepeatTable,
                                 int ddictIsCold, int nbSeq)
{
    switch(type)
    {
    case set_rle :
        RETURN_ERROR_IF(!srcSize, srcSize_wrong);
        RETURN_ERROR_IF((*(const BYTE*)src) > max, corruption_detected);
        {   U32 const symbol = *(const BYTE*)src;
            U32 const baseline = baseValue[symbol];
            U32 const nbBits = nbAdditionalBits[symbol];
            ZSTD144_buildSeqTable_rle(DTableSpace, baseline, nbBits);
        }
        *DTablePtr = DTableSpace;
        return 1;
    case set_basic :
        *DTablePtr = defaultTable;
        return 0;
    case set_repeat:
        RETURN_ERROR_IF(!flagRepeatTable, corruption_detected);
        /* prefetch FSE table if used */
        if (ddictIsCold && (nbSeq > 24 /* heuristic */)) {
            const void* const pStart = *DTablePtr;
            size_t const pSize = sizeof(ZSTD144_seqSymbol) * (SEQSYMBOL_TABLE_SIZE(maxLog));
            PREFETCH_AREA(pStart, pSize);
        }
        return 0;
    case set_compressed :
        {   unsigned tableLog;
            S16 norm[MaxSeq+1];
            size_t const headerSize = FSE144_readNCount(norm, &max, &tableLog, src, srcSize);
            RETURN_ERROR_IF(FSE144_isError(headerSize), corruption_detected);
            RETURN_ERROR_IF(tableLog > maxLog, corruption_detected);
            ZSTD144_buildFSETable(DTableSpace, norm, max, baseValue, nbAdditionalBits, tableLog);
            *DTablePtr = DTableSpace;
            return headerSize;
        }
    default :
        assert(0);
        RETURN_ERROR(GENERIC, "impossible");
    }
}

size_t ZSTD144_decodeSeqHeaders(ZSTD144_DCtx* dctx, int* nbSeqPtr,
                             const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* const iend = istart + srcSize;
    const BYTE* ip = istart;
    int nbSeq;
    DEBUGLOG(5, "ZSTD144_decodeSeqHeaders");

    /* check */
    RETURN_ERROR_IF(srcSize < MIN_SEQUENCES_SIZE, srcSize_wrong);

    /* SeqHead */
    nbSeq = *ip++;
    if (!nbSeq) {
        *nbSeqPtr=0;
        RETURN_ERROR_IF(srcSize != 1, srcSize_wrong);
        return 1;
    }
    if (nbSeq > 0x7F) {
        if (nbSeq == 0xFF) {
            RETURN_ERROR_IF(ip+2 > iend, srcSize_wrong);
            nbSeq = MEM_readLE16(ip) + LONGNBSEQ, ip+=2;
        } else {
            RETURN_ERROR_IF(ip >= iend, srcSize_wrong);
            nbSeq = ((nbSeq-0x80)<<8) + *ip++;
        }
    }
    *nbSeqPtr = nbSeq;

    /* FSE table descriptors */
    RETURN_ERROR_IF(ip+1 > iend, srcSize_wrong); /* minimum possible size: 1 byte for symbol encoding types */
    {   symbolEncodingType_e const LLtype = (symbolEncodingType_e)(*ip >> 6);
        symbolEncodingType_e const OFtype = (symbolEncodingType_e)((*ip >> 4) & 3);
        symbolEncodingType_e const MLtype = (symbolEncodingType_e)((*ip >> 2) & 3);
        ip++;

        /* Build DTables */
        {   size_t const llhSize = ZSTD144_buildSeqTable(dctx->entropy.LLTable, &dctx->LLTptr,
                                                      LLtype, MaxLL, LLFSELog,
                                                      ip, iend-ip,
                                                      LL144_base, LL144_bits,
                                                      LL144_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq);
            RETURN_ERROR_IF(ZSTD144_isError(llhSize), corruption_detected);
            ip += llhSize;
        }

        {   size_t const ofhSize = ZSTD144_buildSeqTable(dctx->entropy.OFTable, &dctx->OFTptr,
                                                      OFtype, MaxOff, OffFSELog,
                                                      ip, iend-ip,
                                                      OF144_base, OF144_bits,
                                                      OF144_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq);
            RETURN_ERROR_IF(ZSTD144_isError(ofhSize), corruption_detected);
            ip += ofhSize;
        }

        {   size_t const mlhSize = ZSTD144_buildSeqTable(dctx->entropy.MLTable, &dctx->MLTptr,
                                                      MLtype, MaxML, MLFSELog,
                                                      ip, iend-ip,
                                                      ML144_base, ML144_bits,
                                                      ML144_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq);
            RETURN_ERROR_IF(ZSTD144_isError(mlhSize), corruption_detected);
            ip += mlhSize;
        }
    }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t matchLength;
    size_t offset;
    const BYTE* match;
} seq_t;

typedef struct {
    size_t state;
    const ZSTD144_seqSymbol* table;
} ZSTD144_fseState;

typedef struct {
    BIT144_DStream_t DStream;
    ZSTD144_fseState stateLL;
    ZSTD144_fseState stateOffb;
    ZSTD144_fseState stateML;
    size_t prevOffset[ZSTD144_REP_NUM];
    const BYTE* prefixStart;
    const BYTE* dictEnd;
    size_t pos;
} seqState_t;

/*! ZSTD144_overlapCopy8() :
 *  Copies 8 bytes from ip to op and updates op and ip where ip <= op.
 *  If the offset is < 8 then the offset is spread to at least 8 bytes.
 *
 *  Precondition: *ip <= *op
 *  Postcondition: *op - *op >= 8
 */
static void ZSTD144_overlapCopy8(BYTE** op, BYTE const** ip, size_t offset) {
    assert(*ip <= *op);
    if (offset < 8) {
        /* close range match, overlap */
        static const U32 dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };   /* added */
        static const int dec64table[] = { 8, 8, 8, 7, 8, 9,10,11 };   /* subtracted */
        int const sub2 = dec64table[offset];
        (*op)[0] = (*ip)[0];
        (*op)[1] = (*ip)[1];
        (*op)[2] = (*ip)[2];
        (*op)[3] = (*ip)[3];
        *ip += dec32table[offset];
        ZSTD144_copy4(*op+4, *ip);
        *ip -= sub2;
    } else {
        ZSTD144_copy8(*op, *ip);
    }
    *ip += 8;
    *op += 8;
    assert(*op - *ip >= 8);
}

/*! ZSTD144_safecopy() :
 *  Specialized version of memcpy() that is allowed to READ up to WILDCOPY_OVERLENGTH past the input buffer
 *  and write up to 16 bytes past oend_w (op >= oend_w is allowed).
 *  This function is only called in the uncommon case where the sequence is near the end of the block. It
 *  should be fast for a single long sequence, but can be slow for several short sequences.
 *
 *  @param ovtype controls the overlap detection
 *         - ZSTD144_no_overlap: The source and destination are guaranteed to be at least WILDCOPY_VECLEN bytes apart.
 *         - ZSTD144_overlap_src_before_dst: The src and dst may overlap and may be any distance apart.
 *           The src buffer must be before the dst buffer.
 */
static void ZSTD144_safecopy(BYTE* op, BYTE* const oend_w, BYTE const* ip, ptrdiff_t length, ZSTD144_overlap_e ovtype) {
    ptrdiff_t const diff = op - ip;
    BYTE* const oend = op + length;

    assert((ovtype == ZSTD144_no_overlap && (diff <= -8 || diff >= 8 || op >= oend_w)) ||
           (ovtype == ZSTD144_overlap_src_before_dst && diff >= 0));

    if (length < 8) {
        /* Handle short lengths. */
        while (op < oend) *op++ = *ip++;
        return;
    }
    if (ovtype == ZSTD144_overlap_src_before_dst) {
        /* Copy 8 bytes and ensure the offset >= 8 when there can be overlap. */
        assert(length >= 8);
        ZSTD144_overlapCopy8(&op, &ip, diff);
        assert(op - ip >= 8);
        assert(op <= oend);
    }

    if (oend <= oend_w) {
        /* No risk of overwrite. */
        ZSTD144_wildcopy(op, ip, length, ovtype);
        return;
    }
    if (op <= oend_w) {
        /* Wildcopy until we get close to the end. */
        assert(oend > oend_w);
        ZSTD144_wildcopy(op, ip, oend_w - op, ovtype);
        ip += oend_w - op;
        op = oend_w;
    }
    /* Handle the leftovers. */
    while (op < oend) *op++ = *ip++;
}

/* ZSTD144_execSequenceEnd():
 * This version handles cases that are near the end of the output buffer. It requires
 * more careful checks to make sure there is no overflow. By separating out these hard
 * and unlikely cases, we can speed up the common cases.
 *
 * NOTE: This function needs to be fast for a single long sequence, but doesn't need
 * to be optimized for many small sequences, since those fall into ZSTD144_execSequence().
 */
FORCE_NOINLINE
size_t ZSTD144_execSequenceEnd(BYTE* op,
                            BYTE* const oend, seq_t sequence,
                            const BYTE** litPtr, const BYTE* const litLimit,
                            const BYTE* const prefixStart, const BYTE* const virtualStart, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    BYTE* const oMatchEnd = op + sequenceLength;   /* risk : address space overflow (32-bits) */
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;
    BYTE* const oend_w = oend - WILDCOPY_OVERLENGTH;

    /* bounds checks */
    assert(oLitEnd < oMatchEnd);
    RETURN_ERROR_IF(oMatchEnd > oend, dstSize_tooSmall, "last match must fit within dstBuffer");
    RETURN_ERROR_IF(iLitEnd > litLimit, corruption_detected, "try to read beyond literal buffer");

    /* copy literals */
    ZSTD144_safecopy(op, oend_w, *litPtr, sequence.litLength, ZSTD144_no_overlap);
    op = oLitEnd;
    *litPtr = iLitEnd;

    /* copy Match */
    if (sequence.offset > (size_t)(oLitEnd - prefixStart)) {
        /* offset beyond prefix */
        RETURN_ERROR_IF(sequence.offset > (size_t)(oLitEnd - virtualStart), corruption_detected);
        match = dictEnd - (prefixStart-match);
        if (match + sequence.matchLength <= dictEnd) {
            memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = prefixStart;
    }   }
    ZSTD144_safecopy(op, oend_w, match, sequence.matchLength, ZSTD144_overlap_src_before_dst);
    return sequenceLength;
}

HINT_INLINE
size_t ZSTD144_execSequence(BYTE* op,
                         BYTE* const oend, seq_t sequence,
                         const BYTE** litPtr, const BYTE* const litLimit,
                         const BYTE* const prefixStart, const BYTE* const virtualStart, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    BYTE* const oMatchEnd = op + sequenceLength;   /* risk : address space overflow (32-bits) */
    BYTE* const oend_w = oend - WILDCOPY_OVERLENGTH;
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;

    /* Errors and uncommon cases handled here. */
    assert(oLitEnd < oMatchEnd);
    if (iLitEnd > litLimit || oMatchEnd > oend_w)
        return ZSTD144_execSequenceEnd(op, oend, sequence, litPtr, litLimit, prefixStart, virtualStart, dictEnd);

    /* Assumptions (everything else goes into ZSTD144_execSequenceEnd()) */
    assert(iLitEnd <= litLimit /* Literal length is in bounds */);
    assert(oLitEnd <= oend_w /* Can wildcopy literals */);
    assert(oMatchEnd <= oend_w /* Can wildcopy matches */);

    /* Copy Literals:
     * Split out litLength <= 16 since it is nearly always true. +1.6% on gcc-9.
     * We likely don't need the full 32-byte wildcopy.
     */
    assert(WILDCOPY_OVERLENGTH >= 16);
    ZSTD144_copy16(op, (*litPtr));
    if (sequence.litLength > 16) {
        ZSTD144_wildcopy(op+16, (*litPtr)+16, sequence.litLength-16, ZSTD144_no_overlap);
    }
    op = oLitEnd;
    *litPtr = iLitEnd;   /* update for next sequence */

    /* Copy Match */
    if (sequence.offset > (size_t)(oLitEnd - prefixStart)) {
        /* offset beyond prefix -> go into extDict */
        RETURN_ERROR_IF(sequence.offset > (size_t)(oLitEnd - virtualStart), corruption_detected);
        match = dictEnd + (match - prefixStart);
        if (match + sequence.matchLength <= dictEnd) {
            memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = prefixStart;
    }   }
    /* Match within prefix of 1 or more bytes */
    assert(op <= oMatchEnd);
    assert(oMatchEnd <= oend_w);
    assert(match >= prefixStart);
    assert(sequence.matchLength >= 1);

    /* Nearly all offsets are >= WILDCOPY_VECLEN bytes, which means we can use wildcopy
     * without overlap checking.
     */
    if (sequence.offset >= WILDCOPY_VECLEN) {
        /* We bet on a full wildcopy for matches, since we expect matches to be
         * longer than literals (in general). In silesia, ~10% of matches are longer
         * than 16 bytes.
         */
        ZSTD144_wildcopy(op, match, (ptrdiff_t)sequence.matchLength, ZSTD144_no_overlap);
        return sequenceLength;
    }
    assert(sequence.offset < WILDCOPY_VECLEN);

    /* Copy 8 bytes and spread the offset to be >= 8. */
    ZSTD144_overlapCopy8(&op, &match, sequence.offset);

    /* If the match length is > 8 bytes, then continue with the wildcopy. */
    if (sequence.matchLength > 8) {
        assert(op < oMatchEnd);
        ZSTD144_wildcopy(op, match, (ptrdiff_t)sequence.matchLength-8, ZSTD144_overlap_src_before_dst);
    }
    return sequenceLength;
}

static void
ZSTD144_initFseState(ZSTD144_fseState* DStatePtr, BIT144_DStream_t* bitD, const ZSTD144_seqSymbol* dt)
{
    const void* ptr = dt;
    const ZSTD144_seqSymbol_header* const DTableH = (const ZSTD144_seqSymbol_header*)ptr;
    DStatePtr->state = BIT144_readBits(bitD, DTableH->tableLog);
    DEBUGLOG(6, "ZSTD144_initFseState : val=%u using %u bits",
                (U32)DStatePtr->state, DTableH->tableLog);
    BIT144_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

FORCE_INLINE_TEMPLATE void
ZSTD144_updateFseState(ZSTD144_fseState* DStatePtr, BIT144_DStream_t* bitD)
{
    ZSTD144_seqSymbol const DInfo = DStatePtr->table[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BIT144_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.nextState + lowBits;
}

/* We need to add at most (ZSTD144_WINDOWLOG_MAX_32 - 1) bits to read the maximum
 * offset bits. But we can only read at most (STREAM_ACCUMULATOR_MIN_32 - 1)
 * bits before reloading. This value is the maximum number of bytes we read
 * after reloading when we are decoding long offsets.
 */
#define LONG_OFFSETS_MAX_EXTRA_BITS_32                       \
    (ZSTD144_WINDOWLOG_MAX_32 > STREAM_ACCUMULATOR_MIN_32       \
        ? ZSTD144_WINDOWLOG_MAX_32 - STREAM_ACCUMULATOR_MIN_32  \
        : 0)

typedef enum { ZSTD144_lo_isRegularOffset, ZSTD144_lo_isLongOffset=1 } ZSTD144_longOffset_e;

#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG
FORCE_INLINE_TEMPLATE seq_t
ZSTD144_decodeSequence(seqState_t* seqState, const ZSTD144_longOffset_e longOffsets)
{
    seq_t seq;
    U32 const llBits = seqState->stateLL.table[seqState->stateLL.state].nbAdditionalBits;
    U32 const mlBits = seqState->stateML.table[seqState->stateML.state].nbAdditionalBits;
    U32 const ofBits = seqState->stateOffb.table[seqState->stateOffb.state].nbAdditionalBits;
    U32 const totalBits = llBits+mlBits+ofBits;
    U32 const llBase = seqState->stateLL.table[seqState->stateLL.state].baseValue;
    U32 const mlBase = seqState->stateML.table[seqState->stateML.state].baseValue;
    U32 const ofBase = seqState->stateOffb.table[seqState->stateOffb.state].baseValue;

    /* sequence */
    {   size_t offset;
        if (!ofBits)
            offset = 0;
        else {
            ZSTD144_STATIC_ASSERT(ZSTD144_lo_isLongOffset == 1);
            ZSTD144_STATIC_ASSERT(LONG_OFFSETS_MAX_EXTRA_BITS_32 == 5);
            assert(ofBits <= MaxOff);
            if (MEM_32bits() && longOffsets && (ofBits >= STREAM_ACCUMULATOR_MIN_32)) {
                U32 const extraBits = ofBits - MIN(ofBits, 32 - seqState->DStream.bitsConsumed);
                offset = ofBase + (BIT144_readBitsFast(&seqState->DStream, ofBits - extraBits) << extraBits);
                BIT144_reloadDStream(&seqState->DStream);
                if (extraBits) offset += BIT144_readBitsFast(&seqState->DStream, extraBits);
                assert(extraBits <= LONG_OFFSETS_MAX_EXTRA_BITS_32);   /* to avoid another reload */
            } else {
                offset = ofBase + BIT144_readBitsFast(&seqState->DStream, ofBits/*>0*/);   /* <=  (ZSTD144_WINDOWLOG_MAX-1) bits */
                if (MEM_32bits()) BIT144_reloadDStream(&seqState->DStream);
            }
        }

        if (ofBits <= 1) {
            offset += (llBase==0);
            if (offset) {
                size_t temp = (offset==3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
                temp += !temp;   /* 0 is not valid; input is corrupted; force offset to 1 */
                if (offset != 1) seqState->prevOffset[2] = seqState->prevOffset[1];
                seqState->prevOffset[1] = seqState->prevOffset[0];
                seqState->prevOffset[0] = offset = temp;
            } else {  /* offset == 0 */
                offset = seqState->prevOffset[0];
            }
        } else {
            seqState->prevOffset[2] = seqState->prevOffset[1];
            seqState->prevOffset[1] = seqState->prevOffset[0];
            seqState->prevOffset[0] = offset;
        }
        seq.offset = offset;
    }

    seq.matchLength = mlBase
                    + ((mlBits>0) ? BIT144_readBitsFast(&seqState->DStream, mlBits/*>0*/) : 0);  /* <=  16 bits */
    if (MEM_32bits() && (mlBits+llBits >= STREAM_ACCUMULATOR_MIN_32-LONG_OFFSETS_MAX_EXTRA_BITS_32))
        BIT144_reloadDStream(&seqState->DStream);
    if (MEM_64bits() && (totalBits >= STREAM_ACCUMULATOR_MIN_64-(LLFSELog+MLFSELog+OffFSELog)))
        BIT144_reloadDStream(&seqState->DStream);
    /* Ensure there are enough bits to read the rest of data in 64-bit mode. */
    ZSTD144_STATIC_ASSERT(16+LLFSELog+MLFSELog+OffFSELog < STREAM_ACCUMULATOR_MIN_64);

    seq.litLength = llBase
                  + ((llBits>0) ? BIT144_readBitsFast(&seqState->DStream, llBits/*>0*/) : 0);    /* <=  16 bits */
    if (MEM_32bits())
        BIT144_reloadDStream(&seqState->DStream);

    DEBUGLOG(6, "seq: litL=%u, matchL=%u, offset=%u",
                (U32)seq.litLength, (U32)seq.matchLength, (U32)seq.offset);

    /* ANS state update */
    ZSTD144_updateFseState(&seqState->stateLL, &seqState->DStream);    /* <=  9 bits */
    ZSTD144_updateFseState(&seqState->stateML, &seqState->DStream);    /* <=  9 bits */
    if (MEM_32bits()) BIT144_reloadDStream(&seqState->DStream);    /* <= 18 bits */
    ZSTD144_updateFseState(&seqState->stateOffb, &seqState->DStream);  /* <=  8 bits */

    return seq;
}

FORCE_INLINE_TEMPLATE size_t
DONT_VECTORIZE
ZSTD144_decompressSequences_body( ZSTD144_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize, int nbSeq,
                         const ZSTD144_longOffset_e isLongOffset)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    const BYTE* const prefixStart = (const BYTE*) (dctx->prefixStart);
    const BYTE* const vBase = (const BYTE*) (dctx->virtualStart);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);
    DEBUGLOG(5, "ZSTD144_decompressSequences_body");

    /* Regen sequences */
    if (nbSeq) {
        seqState_t seqState;
        dctx->fseEntropy = 1;
        { U32 i; for (i=0; i<ZSTD144_REP_NUM; i++) seqState.prevOffset[i] = dctx->entropy.rep[i]; }
        RETURN_ERROR_IF(
            ERR144_isError(BIT144_initDStream(&seqState.DStream, ip, iend-ip)),
            corruption_detected);
        ZSTD144_initFseState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
        ZSTD144_initFseState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
        ZSTD144_initFseState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

        ZSTD144_STATIC_ASSERT(
                BIT144_DStream_unfinished < BIT144_DStream_completed &&
                BIT144_DStream_endOfBuffer < BIT144_DStream_completed &&
                BIT144_DStream_completed < BIT144_DStream_overflow);

        for ( ; (BIT144_reloadDStream(&(seqState.DStream)) <= BIT144_DStream_completed) && nbSeq ; ) {
            nbSeq--;
            {   seq_t const sequence = ZSTD144_decodeSequence(&seqState, isLongOffset);
                size_t const oneSeqSize = ZSTD144_execSequence(op, oend, sequence, &litPtr, litEnd, prefixStart, vBase, dictEnd);
                DEBUGLOG(6, "regenerated sequence size : %u", (U32)oneSeqSize);
                if (ZSTD144_isError(oneSeqSize)) return oneSeqSize;
                op += oneSeqSize;
        }   }

        /* check if reached exact end */
        DEBUGLOG(5, "ZSTD144_decompressSequences_body: after decode loop, remaining nbSeq : %i", nbSeq);
        RETURN_ERROR_IF(nbSeq, corruption_detected);
        RETURN_ERROR_IF(BIT144_reloadDStream(&seqState.DStream) < BIT144_DStream_completed, corruption_detected);
        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTD144_REP_NUM; i++) dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        RETURN_ERROR_IF(lastLLSize > (size_t)(oend-op), dstSize_tooSmall);
        memcpy(op, litPtr, lastLLSize);
        op += lastLLSize;
    }

    return op-ostart;
}

static size_t
ZSTD144_decompressSequences_default(ZSTD144_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD144_longOffset_e isLongOffset)
{
    return ZSTD144_decompressSequences_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG */



#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT
FORCE_INLINE_TEMPLATE seq_t
ZSTD144_decodeSequenceLong(seqState_t* seqState, ZSTD144_longOffset_e const longOffsets)
{
    seq_t seq;
    U32 const llBits = seqState->stateLL.table[seqState->stateLL.state].nbAdditionalBits;
    U32 const mlBits = seqState->stateML.table[seqState->stateML.state].nbAdditionalBits;
    U32 const ofBits = seqState->stateOffb.table[seqState->stateOffb.state].nbAdditionalBits;
    U32 const totalBits = llBits+mlBits+ofBits;
    U32 const llBase = seqState->stateLL.table[seqState->stateLL.state].baseValue;
    U32 const mlBase = seqState->stateML.table[seqState->stateML.state].baseValue;
    U32 const ofBase = seqState->stateOffb.table[seqState->stateOffb.state].baseValue;

    /* sequence */
    {   size_t offset;
        if (!ofBits)
            offset = 0;
        else {
            ZSTD144_STATIC_ASSERT(ZSTD144_lo_isLongOffset == 1);
            ZSTD144_STATIC_ASSERT(LONG_OFFSETS_MAX_EXTRA_BITS_32 == 5);
            assert(ofBits <= MaxOff);
            if (MEM_32bits() && longOffsets) {
                U32 const extraBits = ofBits - MIN(ofBits, STREAM_ACCUMULATOR_MIN_32-1);
                offset = ofBase + (BIT144_readBitsFast(&seqState->DStream, ofBits - extraBits) << extraBits);
                if (MEM_32bits() || extraBits) BIT144_reloadDStream(&seqState->DStream);
                if (extraBits) offset += BIT144_readBitsFast(&seqState->DStream, extraBits);
            } else {
                offset = ofBase + BIT144_readBitsFast(&seqState->DStream, ofBits);   /* <=  (ZSTD144_WINDOWLOG_MAX-1) bits */
                if (MEM_32bits()) BIT144_reloadDStream(&seqState->DStream);
            }
        }

        if (ofBits <= 1) {
            offset += (llBase==0);
            if (offset) {
                size_t temp = (offset==3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
                temp += !temp;   /* 0 is not valid; input is corrupted; force offset to 1 */
                if (offset != 1) seqState->prevOffset[2] = seqState->prevOffset[1];
                seqState->prevOffset[1] = seqState->prevOffset[0];
                seqState->prevOffset[0] = offset = temp;
            } else {
                offset = seqState->prevOffset[0];
            }
        } else {
            seqState->prevOffset[2] = seqState->prevOffset[1];
            seqState->prevOffset[1] = seqState->prevOffset[0];
            seqState->prevOffset[0] = offset;
        }
        seq.offset = offset;
    }

    seq.matchLength = mlBase + ((mlBits>0) ? BIT144_readBitsFast(&seqState->DStream, mlBits) : 0);  /* <=  16 bits */
    if (MEM_32bits() && (mlBits+llBits >= STREAM_ACCUMULATOR_MIN_32-LONG_OFFSETS_MAX_EXTRA_BITS_32))
        BIT144_reloadDStream(&seqState->DStream);
    if (MEM_64bits() && (totalBits >= STREAM_ACCUMULATOR_MIN_64-(LLFSELog+MLFSELog+OffFSELog)))
        BIT144_reloadDStream(&seqState->DStream);
    /* Verify that there is enough bits to read the rest of the data in 64-bit mode. */
    ZSTD144_STATIC_ASSERT(16+LLFSELog+MLFSELog+OffFSELog < STREAM_ACCUMULATOR_MIN_64);

    seq.litLength = llBase + ((llBits>0) ? BIT144_readBitsFast(&seqState->DStream, llBits) : 0);    /* <=  16 bits */
    if (MEM_32bits())
        BIT144_reloadDStream(&seqState->DStream);

    {   size_t const pos = seqState->pos + seq.litLength;
        const BYTE* const matchBase = (seq.offset > pos) ? seqState->dictEnd : seqState->prefixStart;
        seq.match = matchBase + pos - seq.offset;  /* note : this operation can overflow when seq.offset is really too large, which can only happen when input is corrupted.
                                                    * No consequence though : no memory access will occur, overly large offset will be detected in ZSTD144_execSequenceLong() */
        seqState->pos = pos + seq.matchLength;
    }

    /* ANS state update */
    ZSTD144_updateFseState(&seqState->stateLL, &seqState->DStream);    /* <=  9 bits */
    ZSTD144_updateFseState(&seqState->stateML, &seqState->DStream);    /* <=  9 bits */
    if (MEM_32bits()) BIT144_reloadDStream(&seqState->DStream);    /* <= 18 bits */
    ZSTD144_updateFseState(&seqState->stateOffb, &seqState->DStream);  /* <=  8 bits */

    return seq;
}

FORCE_INLINE_TEMPLATE size_t
ZSTD144_decompressSequencesLong_body(
                               ZSTD144_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize, int nbSeq,
                         const ZSTD144_longOffset_e isLongOffset)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    const BYTE* const prefixStart = (const BYTE*) (dctx->prefixStart);
    const BYTE* const dictStart = (const BYTE*) (dctx->virtualStart);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);

    /* Regen sequences */
    if (nbSeq) {
#define STORED_SEQS 4
#define STORED_SEQS_MASK (STORED_SEQS-1)
#define ADVANCED_SEQS 4
        seq_t sequences[STORED_SEQS];
        int const seqAdvance = MIN(nbSeq, ADVANCED_SEQS);
        seqState_t seqState;
        int seqNb;
        dctx->fseEntropy = 1;
        { int i; for (i=0; i<ZSTD144_REP_NUM; i++) seqState.prevOffset[i] = dctx->entropy.rep[i]; }
        seqState.prefixStart = prefixStart;
        seqState.pos = (size_t)(op-prefixStart);
        seqState.dictEnd = dictEnd;
        assert(iend >= ip);
        RETURN_ERROR_IF(
            ERR144_isError(BIT144_initDStream(&seqState.DStream, ip, iend-ip)),
            corruption_detected);
        ZSTD144_initFseState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
        ZSTD144_initFseState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
        ZSTD144_initFseState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

        /* prepare in advance */
        for (seqNb=0; (BIT144_reloadDStream(&seqState.DStream) <= BIT144_DStream_completed) && (seqNb<seqAdvance); seqNb++) {
            sequences[seqNb] = ZSTD144_decodeSequenceLong(&seqState, isLongOffset);
            PREFETCH_L1(sequences[seqNb].match); PREFETCH_L1(sequences[seqNb].match + sequences[seqNb].matchLength - 1); /* note : it's safe to invoke PREFETCH() on any memory address, including invalid ones */
        }
        RETURN_ERROR_IF(seqNb<seqAdvance, corruption_detected);

        /* decode and decompress */
        for ( ; (BIT144_reloadDStream(&(seqState.DStream)) <= BIT144_DStream_completed) && (seqNb<nbSeq) ; seqNb++) {
            seq_t const sequence = ZSTD144_decodeSequenceLong(&seqState, isLongOffset);
            size_t const oneSeqSize = ZSTD144_execSequence(op, oend, sequences[(seqNb-ADVANCED_SEQS) & STORED_SEQS_MASK], &litPtr, litEnd, prefixStart, dictStart, dictEnd);
            if (ZSTD144_isError(oneSeqSize)) return oneSeqSize;
            PREFETCH_L1(sequence.match); PREFETCH_L1(sequence.match + sequence.matchLength - 1); /* note : it's safe to invoke PREFETCH() on any memory address, including invalid ones */
            sequences[seqNb & STORED_SEQS_MASK] = sequence;
            op += oneSeqSize;
        }
        RETURN_ERROR_IF(seqNb<nbSeq, corruption_detected);

        /* finish queue */
        seqNb -= seqAdvance;
        for ( ; seqNb<nbSeq ; seqNb++) {
            size_t const oneSeqSize = ZSTD144_execSequence(op, oend, sequences[seqNb&STORED_SEQS_MASK], &litPtr, litEnd, prefixStart, dictStart, dictEnd);
            if (ZSTD144_isError(oneSeqSize)) return oneSeqSize;
            op += oneSeqSize;
        }

        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTD144_REP_NUM; i++) dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        RETURN_ERROR_IF(lastLLSize > (size_t)(oend-op), dstSize_tooSmall);
        memcpy(op, litPtr, lastLLSize);
        op += lastLLSize;
    }

    return op-ostart;
}

static size_t
ZSTD144_decompressSequencesLong_default(ZSTD144_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD144_longOffset_e isLongOffset)
{
    return ZSTD144_decompressSequencesLong_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT */



#if DYNAMIC_BMI2

#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG
static TARGET_ATTRIBUTE("bmi2") size_t
DONT_VECTORIZE
ZSTD144_decompressSequences_bmi2(ZSTD144_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD144_longOffset_e isLongOffset)
{
    return ZSTD144_decompressSequences_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG */

#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT
static TARGET_ATTRIBUTE("bmi2") size_t
ZSTD144_decompressSequencesLong_bmi2(ZSTD144_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD144_longOffset_e isLongOffset)
{
    return ZSTD144_decompressSequencesLong_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT */

#endif /* DYNAMIC_BMI2 */

typedef size_t (*ZSTD144_decompressSequences_t)(
                            ZSTD144_DCtx* dctx,
                            void* dst, size_t maxDstSize,
                            const void* seqStart, size_t seqSize, int nbSeq,
                            const ZSTD144_longOffset_e isLongOffset);

#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG
static size_t
ZSTD144_decompressSequences(ZSTD144_DCtx* dctx, void* dst, size_t maxDstSize,
                   const void* seqStart, size_t seqSize, int nbSeq,
                   const ZSTD144_longOffset_e isLongOffset)
{
    DEBUGLOG(5, "ZSTD144_decompressSequences");
#if DYNAMIC_BMI2
    if (dctx->bmi2) {
        return ZSTD144_decompressSequences_bmi2(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
    }
#endif
  return ZSTD144_decompressSequences_default(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG */


#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT
/* ZSTD144_decompressSequencesLong() :
 * decompression function triggered when a minimum share of offsets is considered "long",
 * aka out of cache.
 * note : "long" definition seems overloaded here, sometimes meaning "wider than bitstream register", and sometimes meaning "farther than memory cache distance".
 * This function will try to mitigate main memory latency through the use of prefetching */
static size_t
ZSTD144_decompressSequencesLong(ZSTD144_DCtx* dctx,
                             void* dst, size_t maxDstSize,
                             const void* seqStart, size_t seqSize, int nbSeq,
                             const ZSTD144_longOffset_e isLongOffset)
{
    DEBUGLOG(5, "ZSTD144_decompressSequencesLong");
#if DYNAMIC_BMI2
    if (dctx->bmi2) {
        return ZSTD144_decompressSequencesLong_bmi2(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
    }
#endif
  return ZSTD144_decompressSequencesLong_default(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset);
}
#endif /* ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT */



#if !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG)
/* ZSTD144_getLongOffsetsShare() :
 * condition : offTable must be valid
 * @return : "share" of long offsets (arbitrarily defined as > (1<<23))
 *           compared to maximum possible of (1<<OffFSELog) */
static unsigned
ZSTD144_getLongOffsetsShare(const ZSTD144_seqSymbol* offTable)
{
    const void* ptr = offTable;
    U32 const tableLog = ((const ZSTD144_seqSymbol_header*)ptr)[0].tableLog;
    const ZSTD144_seqSymbol* table = offTable + 1;
    U32 const max = 1 << tableLog;
    U32 u, total = 0;
    DEBUGLOG(5, "ZSTD144_getLongOffsetsShare: (tableLog=%u)", tableLog);

    assert(max <= (1 << OffFSELog));  /* max not too large */
    for (u=0; u<max; u++) {
        if (table[u].nbAdditionalBits > 22) total += 1;
    }

    assert(tableLog <= OffFSELog);
    total <<= (OffFSELog - tableLog);  /* scale to OffFSELog */

    return total;
}
#endif


size_t
ZSTD144_decompressBlock_internal(ZSTD144_DCtx* dctx,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize, const int frame)
{   /* blockType == blockCompressed */
    const BYTE* ip = (const BYTE*)src;
    /* isLongOffset must be true if there are long offsets.
     * Offsets are long if they are larger than 2^STREAM_ACCUMULATOR_MIN.
     * We don't expect that to be the case in 64-bit mode.
     * In block mode, window size is not known, so we have to be conservative.
     * (note: but it could be evaluated from current-lowLimit)
     */
    ZSTD144_longOffset_e const isLongOffset = (ZSTD144_longOffset_e)(MEM_32bits() && (!frame || (dctx->fParams.windowSize > (1ULL << STREAM_ACCUMULATOR_MIN))));
    DEBUGLOG(5, "ZSTD144_decompressBlock_internal (size : %u)", (U32)srcSize);

    RETURN_ERROR_IF(srcSize >= ZSTD144_BLOCKSIZE_MAX, srcSize_wrong);

    /* Decode literals section */
    {   size_t const litCSize = ZSTD144_decodeLiteralsBlock(dctx, src, srcSize);
        DEBUGLOG(5, "ZSTD144_decodeLiteralsBlock : %u", (U32)litCSize);
        if (ZSTD144_isError(litCSize)) return litCSize;
        ip += litCSize;
        srcSize -= litCSize;
    }

    /* Build Decoding Tables */
    {
        /* These macros control at build-time which decompressor implementation
         * we use. If neither is defined, we do some inspection and dispatch at
         * runtime.
         */
#if !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG)
        int usePrefetchDecoder = dctx->ddictIsCold;
#endif
        int nbSeq;
        size_t const seqHSize = ZSTD144_decodeSeqHeaders(dctx, &nbSeq, ip, srcSize);
        if (ZSTD144_isError(seqHSize)) return seqHSize;
        ip += seqHSize;
        srcSize -= seqHSize;

#if !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG)
        if ( !usePrefetchDecoder
          && (!frame || (dctx->fParams.windowSize > (1<<24)))
          && (nbSeq>ADVANCED_SEQS) ) {  /* could probably use a larger nbSeq limit */
            U32 const shareLongOffsets = ZSTD144_getLongOffsetsShare(dctx->OFTptr);
            U32 const minShare = MEM_64bits() ? 7 : 20; /* heuristic values, correspond to 2.73% and 7.81% */
            usePrefetchDecoder = (shareLongOffsets >= minShare);
        }
#endif

        dctx->ddictIsCold = 0;

#if !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG)
        if (usePrefetchDecoder)
#endif
#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_SHORT
            return ZSTD144_decompressSequencesLong(dctx, dst, dstCapacity, ip, srcSize, nbSeq, isLongOffset);
#endif

#ifndef ZSTD144_FORCE_DECOMPRESS_SEQUENCES_LONG
        /* else */
        return ZSTD144_decompressSequences(dctx, dst, dstCapacity, ip, srcSize, nbSeq, isLongOffset);
#endif
    }
}


size_t ZSTD144_decompressBlock(ZSTD144_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{
    size_t dSize;
    ZSTD144_checkContinuity(dctx, dst);
    dSize = ZSTD144_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize, /* frame */ 0);
    dctx->previousDstEnd = (char*)dst + dSize;
    return dSize;
}
