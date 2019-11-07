/* ******************************************************************
   FSE : Finite State Entropy decoder
   Copyright (C) 2013-2015, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */


/* **************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>     /* malloc, free, qsort */
#include <string.h>     /* memcpy, memset */
#include "bitstream.h"
#include "compiler.h"
#define FSE144_STATIC_LINKING_ONLY
#include "fse.h"
#include "error_private.h"


/* **************************************************************
*  Error Management
****************************************************************/
#define FSE144_isError ERR144_isError
#define FSE144_STATIC_ASSERT(c) DEBUG_STATIC_ASSERT(c)   /* use only *after* variable declarations */

/* check and forward error code */
#ifndef CHECK_F
#define CHECK_F(f) { size_t const e = f; if (FSE144_isError(e)) return e; }
#endif


/* **************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef FSE144_FUNCTION_EXTENSION
#  error "FSE144_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE144_FUNCTION_TYPE
#  error "FSE144_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define FSE144_CAT(X,Y) X##Y
#define FSE144_FUNCTION_NAME(X,Y) FSE144_CAT(X,Y)
#define FSE144_TYPE_NAME(X,Y) FSE144_CAT(X,Y)


/* Function templates */
FSE144_DTable* FSE144_createDTable (unsigned tableLog)
{
    if (tableLog > FSE144_TABLELOG_ABSOLUTE_MAX) tableLog = FSE144_TABLELOG_ABSOLUTE_MAX;
    return (FSE144_DTable*)malloc( FSE144_DTABLE_SIZE_U32(tableLog) * sizeof (U32) );
}

void FSE144_freeDTable (FSE144_DTable* dt)
{
    free(dt);
}

size_t FSE144_buildDTable(FSE144_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    void* const tdPtr = dt+1;   /* because *dt is unsigned, 32-bits aligned on 32-bits */
    FSE144_DECODE_TYPE* const tableDecode = (FSE144_DECODE_TYPE*) (tdPtr);
    U16 symbolNext[FSE144_MAX_SYMBOL_VALUE+1];

    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;
    U32 highThreshold = tableSize-1;

    /* Sanity Checks */
    if (maxSymbolValue > FSE144_MAX_SYMBOL_VALUE) return ERROR(maxSymbolValue_tooLarge);
    if (tableLog > FSE144_MAX_TABLELOG) return ERROR(tableLog_tooLarge);

    /* Init, lay down lowprob symbols */
    {   FSE144_DTableHeader DTableH;
        DTableH.tableLog = (U16)tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].symbol = (FSE144_FUNCTION_TYPE)s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    symbolNext[s] = normalizedCounter[s];
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
                tableDecode[position].symbol = (FSE144_FUNCTION_TYPE)s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }
        if (position!=0) return ERROR(GENERIC);   /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {   U32 u;
        for (u=0; u<tableSize; u++) {
            FSE144_FUNCTION_TYPE const symbol = (FSE144_FUNCTION_TYPE)(tableDecode[u].symbol);
            U32 const nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - BIT144_highbit32(nextState) );
            tableDecode[u].newState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
    }   }

    return 0;
}


#ifndef FSE144_COMMONDEFS_ONLY

/*-*******************************************************
*  Decompression (Byte symbols)
*********************************************************/
size_t FSE144_buildDTable_rle (FSE144_DTable* dt, BYTE symbolValue)
{
    void* ptr = dt;
    FSE144_DTableHeader* const DTableH = (FSE144_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSE144_decode_t* const cell = (FSE144_decode_t*)dPtr;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->newState = 0;
    cell->symbol = symbolValue;
    cell->nbBits = 0;

    return 0;
}


size_t FSE144_buildDTable_raw (FSE144_DTable* dt, unsigned nbBits)
{
    void* ptr = dt;
    FSE144_DTableHeader* const DTableH = (FSE144_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSE144_decode_t* const dinfo = (FSE144_decode_t*)dPtr;
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSV1 = tableMask+1;
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return ERROR(GENERIC);         /* min size */

    /* Build Decoding Table */
    DTableH->tableLog = (U16)nbBits;
    DTableH->fastMode = 1;
    for (s=0; s<maxSV1; s++) {
        dinfo[s].newState = 0;
        dinfo[s].symbol = (BYTE)s;
        dinfo[s].nbBits = (BYTE)nbBits;
    }

    return 0;
}

FORCE_INLINE_TEMPLATE size_t FSE144_decompress_usingDTable_generic(
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const FSE144_DTable* dt, const unsigned fast)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-3;

    BIT144_DStream_t bitD;
    FSE144_DState_t state1;
    FSE144_DState_t state2;

    /* Init */
    CHECK_F(BIT144_initDStream(&bitD, cSrc, cSrcSize));

    FSE144_initDState(&state1, &bitD, dt);
    FSE144_initDState(&state2, &bitD, dt);

#define FSE144_GETSYMBOL(statePtr) fast ? FSE144_decodeSymbolFast(statePtr, &bitD) : FSE144_decodeSymbol(statePtr, &bitD)

    /* 4 symbols per loop */
    for ( ; (BIT144_reloadDStream(&bitD)==BIT144_DStream_unfinished) & (op<olimit) ; op+=4) {
        op[0] = FSE144_GETSYMBOL(&state1);

        if (FSE144_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BIT144_reloadDStream(&bitD);

        op[1] = FSE144_GETSYMBOL(&state2);

        if (FSE144_MAX_TABLELOG*4+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            { if (BIT144_reloadDStream(&bitD) > BIT144_DStream_unfinished) { op+=2; break; } }

        op[2] = FSE144_GETSYMBOL(&state1);

        if (FSE144_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BIT144_reloadDStream(&bitD);

        op[3] = FSE144_GETSYMBOL(&state2);
    }

    /* tail */
    /* note : BIT144_reloadDStream(&bitD) >= FSE144_DStream_partiallyFilled; Ends at exactly BIT144_DStream_completed */
    while (1) {
        if (op>(omax-2)) return ERROR(dstSize_tooSmall);
        *op++ = FSE144_GETSYMBOL(&state1);
        if (BIT144_reloadDStream(&bitD)==BIT144_DStream_overflow) {
            *op++ = FSE144_GETSYMBOL(&state2);
            break;
        }

        if (op>(omax-2)) return ERROR(dstSize_tooSmall);
        *op++ = FSE144_GETSYMBOL(&state2);
        if (BIT144_reloadDStream(&bitD)==BIT144_DStream_overflow) {
            *op++ = FSE144_GETSYMBOL(&state1);
            break;
    }   }

    return op-ostart;
}


size_t FSE144_decompress_usingDTable(void* dst, size_t originalSize,
                            const void* cSrc, size_t cSrcSize,
                            const FSE144_DTable* dt)
{
    const void* ptr = dt;
    const FSE144_DTableHeader* DTableH = (const FSE144_DTableHeader*)ptr;
    const U32 fastMode = DTableH->fastMode;

    /* select fast mode (static) */
    if (fastMode) return FSE144_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
    return FSE144_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}


size_t FSE144_decompress_wksp(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, FSE144_DTable* workSpace, unsigned maxLog)
{
    const BYTE* const istart = (const BYTE*)cSrc;
    const BYTE* ip = istart;
    short counting[FSE144_MAX_SYMBOL_VALUE+1];
    unsigned tableLog;
    unsigned maxSymbolValue = FSE144_MAX_SYMBOL_VALUE;

    /* normal FSE decoding mode */
    size_t const NCountLength = FSE144_readNCount (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
    if (FSE144_isError(NCountLength)) return NCountLength;
    //if (NCountLength >= cSrcSize) return ERROR(srcSize_wrong);   /* too small input size; supposed to be already checked in NCountLength, only remaining case : NCountLength==cSrcSize */
    if (tableLog > maxLog) return ERROR(tableLog_tooLarge);
    ip += NCountLength;
    cSrcSize -= NCountLength;

    CHECK_F( FSE144_buildDTable (workSpace, counting, maxSymbolValue, tableLog) );

    return FSE144_decompress_usingDTable (dst, dstCapacity, ip, cSrcSize, workSpace);   /* always return, even if it is an error code */
}


typedef FSE144_DTable DTable_max_t[FSE144_DTABLE_SIZE_U32(FSE144_MAX_TABLELOG)];

size_t FSE144_decompress(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize)
{
    DTable_max_t dt;   /* Static analyzer seems unable to understand this table will be properly initialized later */
    return FSE144_decompress_wksp(dst, dstCapacity, cSrc, cSrcSize, dt, FSE144_MAX_TABLELOG);
}



#endif   /* FSE144_COMMONDEFS_ONLY */
