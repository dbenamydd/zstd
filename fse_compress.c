/* ******************************************************************
   FSE : Finite State Entropy encoder
   Copyright (C) 2013-present, Yann Collet.

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
#include "compiler.h"
#include "mem.h"        /* U32, U16, etc. */
#include "debug.h"      /* assert, DEBUGLOG */
#include "hist.h"       /* HIST144_count_wksp */
#include "bitstream.h"
#define FSE144_STATIC_LINKING_ONLY
#include "fse.h"
#include "error_private.h"


/* **************************************************************
*  Error Management
****************************************************************/
#define FSE144_isError ERR144_isError


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

/* FSE144_buildCTable_wksp() :
 * Same as FSE144_buildCTable(), but using an externally allocated scratch buffer (`workSpace`).
 * wkspSize should be sized to handle worst case situation, which is `1<<max_tableLog * sizeof(FSE144_FUNCTION_TYPE)`
 * workSpace must also be properly aligned with FSE144_FUNCTION_TYPE requirements
 */
size_t FSE144_buildCTable_wksp(FSE144_CTable* ct,
                      const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog,
                            void* workSpace, size_t wkspSize)
{
    U32 const tableSize = 1 << tableLog;
    U32 const tableMask = tableSize - 1;
    void* const ptr = ct;
    U16* const tableU16 = ( (U16*) ptr) + 2;
    void* const FSCT = ((U32*)ptr) + 1 /* header */ + (tableLog ? tableSize>>1 : 1) ;
    FSE144_symbolCompressionTransform* const symbolTT = (FSE144_symbolCompressionTransform*) (FSCT);
    U32 const step = FSE144_TABLESTEP(tableSize);
    U32 cumul[FSE144_MAX_SYMBOL_VALUE+2];

    FSE144_FUNCTION_TYPE* const tableSymbol = (FSE144_FUNCTION_TYPE*)workSpace;
    U32 highThreshold = tableSize-1;

    /* CTable header */
    if (((size_t)1 << tableLog) * sizeof(FSE144_FUNCTION_TYPE) > wkspSize) return ERROR(tableLog_tooLarge);
    tableU16[-2] = (U16) tableLog;
    tableU16[-1] = (U16) maxSymbolValue;
    assert(tableLog < 16);   /* required for threshold strategy to work */

    /* For explanations on how to distribute symbol values over the table :
     * http://fastcompression.blogspot.fr/2014/02/fse-distributing-symbol-values.html */

     #ifdef __clang_analyzer__
     memset(tableSymbol, 0, sizeof(*tableSymbol) * tableSize);   /* useless initialization, just to keep scan-build happy */
     #endif

    /* symbol start positions */
    {   U32 u;
        cumul[0] = 0;
        for (u=1; u <= maxSymbolValue+1; u++) {
            if (normalizedCounter[u-1]==-1) {  /* Low proba symbol */
                cumul[u] = cumul[u-1] + 1;
                tableSymbol[highThreshold--] = (FSE144_FUNCTION_TYPE)(u-1);
            } else {
                cumul[u] = cumul[u-1] + normalizedCounter[u-1];
        }   }
        cumul[maxSymbolValue+1] = tableSize+1;
    }

    /* Spread symbols */
    {   U32 position = 0;
        U32 symbol;
        for (symbol=0; symbol<=maxSymbolValue; symbol++) {
            int nbOccurrences;
            int const freq = normalizedCounter[symbol];
            for (nbOccurrences=0; nbOccurrences<freq; nbOccurrences++) {
                tableSymbol[position] = (FSE144_FUNCTION_TYPE)symbol;
                position = (position + step) & tableMask;
                while (position > highThreshold)
                    position = (position + step) & tableMask;   /* Low proba area */
        }   }

        assert(position==0);  /* Must have initialized all positions */
    }

    /* Build table */
    {   U32 u; for (u=0; u<tableSize; u++) {
        FSE144_FUNCTION_TYPE s = tableSymbol[u];   /* note : static analyzer may not understand tableSymbol is properly initialized */
        tableU16[cumul[s]++] = (U16) (tableSize+u);   /* TableU16 : sorted by symbol order; gives next state value */
    }   }

    /* Build Symbol Transformation Table */
    {   unsigned total = 0;
        unsigned s;
        for (s=0; s<=maxSymbolValue; s++) {
            switch (normalizedCounter[s])
            {
            case  0:
                /* filling nonetheless, for compatibility with FSE144_getMaxNbBits() */
                symbolTT[s].deltaNbBits = ((tableLog+1) << 16) - (1<<tableLog);
                break;

            case -1:
            case  1:
                symbolTT[s].deltaNbBits = (tableLog << 16) - (1<<tableLog);
                symbolTT[s].deltaFindState = total - 1;
                total ++;
                break;
            default :
                {
                    U32 const maxBitsOut = tableLog - BIT144_highbit32 (normalizedCounter[s]-1);
                    U32 const minStatePlus = normalizedCounter[s] << maxBitsOut;
                    symbolTT[s].deltaNbBits = (maxBitsOut << 16) - minStatePlus;
                    symbolTT[s].deltaFindState = total - normalizedCounter[s];
                    total +=  normalizedCounter[s];
    }   }   }   }

#if 0  /* debug : symbol costs */
    DEBUGLOG(5, "\n --- table statistics : ");
    {   U32 symbol;
        for (symbol=0; symbol<=maxSymbolValue; symbol++) {
            DEBUGLOG(5, "%3u: w=%3i,   maxBits=%u, fracBits=%.2f",
                symbol, normalizedCounter[symbol],
                FSE144_getMaxNbBits(symbolTT, symbol),
                (double)FSE144_bitCost(symbolTT, tableLog, symbol, 8) / 256);
        }
    }
#endif

    return 0;
}


size_t FSE144_buildCTable(FSE144_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    FSE144_FUNCTION_TYPE tableSymbol[FSE144_MAX_TABLESIZE];   /* memset() is not necessary, even if static analyzer complain about it */
    return FSE144_buildCTable_wksp(ct, normalizedCounter, maxSymbolValue, tableLog, tableSymbol, sizeof(tableSymbol));
}



#ifndef FSE144_COMMONDEFS_ONLY


/*-**************************************************************
*  FSE NCount encoding
****************************************************************/
size_t FSE144_NCountWriteBound(unsigned maxSymbolValue, unsigned tableLog)
{
    size_t const maxHeaderSize = (((maxSymbolValue+1) * tableLog) >> 3) + 3;
    return maxSymbolValue ? maxHeaderSize : FSE144_NCOUNTBOUND;  /* maxSymbolValue==0 ? use default */
}

static size_t
FSE144_writeNCount_generic (void* header, size_t headerBufferSize,
                   const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog,
                         unsigned writeIsSafe)
{
    BYTE* const ostart = (BYTE*) header;
    BYTE* out = ostart;
    BYTE* const oend = ostart + headerBufferSize;
    int nbBits;
    const int tableSize = 1 << tableLog;
    int remaining;
    int threshold;
    U32 bitStream = 0;
    int bitCount = 0;
    unsigned symbol = 0;
    unsigned const alphabetSize = maxSymbolValue + 1;
    int previousIs0 = 0;

    /* Table Size */
    bitStream += (tableLog-FSE144_MIN_TABLELOG) << bitCount;
    bitCount  += 4;

    /* Init */
    remaining = tableSize+1;   /* +1 for extra accuracy */
    threshold = tableSize;
    nbBits = tableLog+1;

    while ((symbol < alphabetSize) && (remaining>1)) {  /* stops at 1 */
        if (previousIs0) {
            unsigned start = symbol;
            while ((symbol < alphabetSize) && !normalizedCounter[symbol]) symbol++;
            if (symbol == alphabetSize) break;   /* incorrect distribution */
            while (symbol >= start+24) {
                start+=24;
                bitStream += 0xFFFFU << bitCount;
                if ((!writeIsSafe) && (out > oend-2))
                    return ERROR(dstSize_tooSmall);   /* Buffer overflow */
                out[0] = (BYTE) bitStream;
                out[1] = (BYTE)(bitStream>>8);
                out+=2;
                bitStream>>=16;
            }
            while (symbol >= start+3) {
                start+=3;
                bitStream += 3 << bitCount;
                bitCount += 2;
            }
            bitStream += (symbol-start) << bitCount;
            bitCount += 2;
            if (bitCount>16) {
                if ((!writeIsSafe) && (out > oend - 2))
                    return ERROR(dstSize_tooSmall);   /* Buffer overflow */
                out[0] = (BYTE)bitStream;
                out[1] = (BYTE)(bitStream>>8);
                out += 2;
                bitStream >>= 16;
                bitCount -= 16;
        }   }
        {   int count = normalizedCounter[symbol++];
            int const max = (2*threshold-1) - remaining;
            remaining -= count < 0 ? -count : count;
            count++;   /* +1 for extra accuracy */
            if (count>=threshold)
                count += max;   /* [0..max[ [max..threshold[ (...) [threshold+max 2*threshold[ */
            bitStream += count << bitCount;
            bitCount  += nbBits;
            bitCount  -= (count<max);
            previousIs0  = (count==1);
            if (remaining<1) return ERROR(GENERIC);
            while (remaining<threshold) { nbBits--; threshold>>=1; }
        }
        if (bitCount>16) {
            if ((!writeIsSafe) && (out > oend - 2))
                return ERROR(dstSize_tooSmall);   /* Buffer overflow */
            out[0] = (BYTE)bitStream;
            out[1] = (BYTE)(bitStream>>8);
            out += 2;
            bitStream >>= 16;
            bitCount -= 16;
    }   }

    if (remaining != 1)
        return ERROR(GENERIC);  /* incorrect normalized distribution */
    assert(symbol <= alphabetSize);

    /* flush remaining bitStream */
    if ((!writeIsSafe) && (out > oend - 2))
        return ERROR(dstSize_tooSmall);   /* Buffer overflow */
    out[0] = (BYTE)bitStream;
    out[1] = (BYTE)(bitStream>>8);
    out+= (bitCount+7) /8;

    return (out-ostart);
}


size_t FSE144_writeNCount (void* buffer, size_t bufferSize,
                  const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    if (tableLog > FSE144_MAX_TABLELOG) return ERROR(tableLog_tooLarge);   /* Unsupported */
    if (tableLog < FSE144_MIN_TABLELOG) return ERROR(GENERIC);   /* Unsupported */

    if (bufferSize < FSE144_NCountWriteBound(maxSymbolValue, tableLog))
        return FSE144_writeNCount_generic(buffer, bufferSize, normalizedCounter, maxSymbolValue, tableLog, 0);

    return FSE144_writeNCount_generic(buffer, bufferSize, normalizedCounter, maxSymbolValue, tableLog, 1 /* write in buffer is safe */);
}


/*-**************************************************************
*  FSE Compression Code
****************************************************************/

FSE144_CTable* FSE144_createCTable (unsigned maxSymbolValue, unsigned tableLog)
{
    size_t size;
    if (tableLog > FSE144_TABLELOG_ABSOLUTE_MAX) tableLog = FSE144_TABLELOG_ABSOLUTE_MAX;
    size = FSE144_CTABLE_SIZE_U32 (tableLog, maxSymbolValue) * sizeof(U32);
    return (FSE144_CTable*)malloc(size);
}

void FSE144_freeCTable (FSE144_CTable* ct) { free(ct); }

/* provides the minimum logSize to safely represent a distribution */
static unsigned FSE144_minTableLog(size_t srcSize, unsigned maxSymbolValue)
{
    U32 minBitsSrc = BIT144_highbit32((U32)(srcSize)) + 1;
    U32 minBitsSymbols = BIT144_highbit32(maxSymbolValue) + 2;
    U32 minBits = minBitsSrc < minBitsSymbols ? minBitsSrc : minBitsSymbols;
    assert(srcSize > 1); /* Not supported, RLE should be used instead */
    return minBits;
}

unsigned FSE144_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus)
{
    U32 maxBitsSrc = BIT144_highbit32((U32)(srcSize - 1)) - minus;
    U32 tableLog = maxTableLog;
    U32 minBits = FSE144_minTableLog(srcSize, maxSymbolValue);
    assert(srcSize > 1); /* Not supported, RLE should be used instead */
    if (tableLog==0) tableLog = FSE144_DEFAULT_TABLELOG;
    if (maxBitsSrc < tableLog) tableLog = maxBitsSrc;   /* Accuracy can be reduced */
    if (minBits > tableLog) tableLog = minBits;   /* Need a minimum to safely represent all symbol values */
    if (tableLog < FSE144_MIN_TABLELOG) tableLog = FSE144_MIN_TABLELOG;
    if (tableLog > FSE144_MAX_TABLELOG) tableLog = FSE144_MAX_TABLELOG;
    return tableLog;
}

unsigned FSE144_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue)
{
    return FSE144_optimalTableLog_internal(maxTableLog, srcSize, maxSymbolValue, 2);
}


/* Secondary normalization method.
   To be used when primary method fails. */

static size_t FSE144_normalizeM2(short* norm, U32 tableLog, const unsigned* count, size_t total, U32 maxSymbolValue)
{
    short const NOT_YET_ASSIGNED = -2;
    U32 s;
    U32 distributed = 0;
    U32 ToDistribute;

    /* Init */
    U32 const lowThreshold = (U32)(total >> tableLog);
    U32 lowOne = (U32)((total * 3) >> (tableLog + 1));

    for (s=0; s<=maxSymbolValue; s++) {
        if (count[s] == 0) {
            norm[s]=0;
            continue;
        }
        if (count[s] <= lowThreshold) {
            norm[s] = -1;
            distributed++;
            total -= count[s];
            continue;
        }
        if (count[s] <= lowOne) {
            norm[s] = 1;
            distributed++;
            total -= count[s];
            continue;
        }

        norm[s]=NOT_YET_ASSIGNED;
    }
    ToDistribute = (1 << tableLog) - distributed;

    if (ToDistribute == 0)
        return 0;

    if ((total / ToDistribute) > lowOne) {
        /* risk of rounding to zero */
        lowOne = (U32)((total * 3) / (ToDistribute * 2));
        for (s=0; s<=maxSymbolValue; s++) {
            if ((norm[s] == NOT_YET_ASSIGNED) && (count[s] <= lowOne)) {
                norm[s] = 1;
                distributed++;
                total -= count[s];
                continue;
        }   }
        ToDistribute = (1 << tableLog) - distributed;
    }

    if (distributed == maxSymbolValue+1) {
        /* all values are pretty poor;
           probably incompressible data (should have already been detected);
           find max, then give all remaining points to max */
        U32 maxV = 0, maxC = 0;
        for (s=0; s<=maxSymbolValue; s++)
            if (count[s] > maxC) { maxV=s; maxC=count[s]; }
        norm[maxV] += (short)ToDistribute;
        return 0;
    }

    if (total == 0) {
        /* all of the symbols were low enough for the lowOne or lowThreshold */
        for (s=0; ToDistribute > 0; s = (s+1)%(maxSymbolValue+1))
            if (norm[s] > 0) { ToDistribute--; norm[s]++; }
        return 0;
    }

    {   U64 const vStepLog = 62 - tableLog;
        U64 const mid = (1ULL << (vStepLog-1)) - 1;
        U64 const rStep = ((((U64)1<<vStepLog) * ToDistribute) + mid) / total;   /* scale on remaining */
        U64 tmpTotal = mid;
        for (s=0; s<=maxSymbolValue; s++) {
            if (norm[s]==NOT_YET_ASSIGNED) {
                U64 const end = tmpTotal + (count[s] * rStep);
                U32 const sStart = (U32)(tmpTotal >> vStepLog);
                U32 const sEnd = (U32)(end >> vStepLog);
                U32 const weight = sEnd - sStart;
                if (weight < 1)
                    return ERROR(GENERIC);
                norm[s] = (short)weight;
                tmpTotal = end;
    }   }   }

    return 0;
}


size_t FSE144_normalizeCount (short* normalizedCounter, unsigned tableLog,
                           const unsigned* count, size_t total,
                           unsigned maxSymbolValue)
{
    /* Sanity checks */
    if (tableLog==0) tableLog = FSE144_DEFAULT_TABLELOG;
    if (tableLog < FSE144_MIN_TABLELOG) return ERROR(GENERIC);   /* Unsupported size */
    if (tableLog > FSE144_MAX_TABLELOG) return ERROR(tableLog_tooLarge);   /* Unsupported size */
    if (tableLog < FSE144_minTableLog(total, maxSymbolValue)) return ERROR(GENERIC);   /* Too small tableLog, compression potentially impossible */

    {   static U32 const rtbTable[] = {     0, 473195, 504333, 520860, 550000, 700000, 750000, 830000 };
        U64 const scale = 62 - tableLog;
        U64 const step = ((U64)1<<62) / total;   /* <== here, one division ! */
        U64 const vStep = 1ULL<<(scale-20);
        int stillToDistribute = 1<<tableLog;
        unsigned s;
        unsigned largest=0;
        short largestP=0;
        U32 lowThreshold = (U32)(total >> tableLog);

        for (s=0; s<=maxSymbolValue; s++) {
            if (count[s] == total) return 0;   /* rle special case */
            if (count[s] == 0) { normalizedCounter[s]=0; continue; }
            if (count[s] <= lowThreshold) {
                normalizedCounter[s] = -1;
                stillToDistribute--;
            } else {
                short proba = (short)((count[s]*step) >> scale);
                if (proba<8) {
                    U64 restToBeat = vStep * rtbTable[proba];
                    proba += (count[s]*step) - ((U64)proba<<scale) > restToBeat;
                }
                if (proba > largestP) { largestP=proba; largest=s; }
                normalizedCounter[s] = proba;
                stillToDistribute -= proba;
        }   }
        if (-stillToDistribute >= (normalizedCounter[largest] >> 1)) {
            /* corner case, need another normalization method */
            size_t const errorCode = FSE144_normalizeM2(normalizedCounter, tableLog, count, total, maxSymbolValue);
            if (FSE144_isError(errorCode)) return errorCode;
        }
        else normalizedCounter[largest] += (short)stillToDistribute;
    }

#if 0
    {   /* Print Table (debug) */
        U32 s;
        U32 nTotal = 0;
        for (s=0; s<=maxSymbolValue; s++)
            RAWLOG(2, "%3i: %4i \n", s, normalizedCounter[s]);
        for (s=0; s<=maxSymbolValue; s++)
            nTotal += abs(normalizedCounter[s]);
        if (nTotal != (1U<<tableLog))
            RAWLOG(2, "Warning !!! Total == %u != %u !!!", nTotal, 1U<<tableLog);
        getchar();
    }
#endif

    return tableLog;
}


/* fake FSE144_CTable, for raw (uncompressed) input */
size_t FSE144_buildCTable_raw (FSE144_CTable* ct, unsigned nbBits)
{
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSymbolValue = tableMask;
    void* const ptr = ct;
    U16* const tableU16 = ( (U16*) ptr) + 2;
    void* const FSCT = ((U32*)ptr) + 1 /* header */ + (tableSize>>1);   /* assumption : tableLog >= 1 */
    FSE144_symbolCompressionTransform* const symbolTT = (FSE144_symbolCompressionTransform*) (FSCT);
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return ERROR(GENERIC);             /* min size */

    /* header */
    tableU16[-2] = (U16) nbBits;
    tableU16[-1] = (U16) maxSymbolValue;

    /* Build table */
    for (s=0; s<tableSize; s++)
        tableU16[s] = (U16)(tableSize + s);

    /* Build Symbol Transformation Table */
    {   const U32 deltaNbBits = (nbBits << 16) - (1 << nbBits);
        for (s=0; s<=maxSymbolValue; s++) {
            symbolTT[s].deltaNbBits = deltaNbBits;
            symbolTT[s].deltaFindState = s-1;
    }   }

    return 0;
}

/* fake FSE144_CTable, for rle input (always same symbol) */
size_t FSE144_buildCTable_rle (FSE144_CTable* ct, BYTE symbolValue)
{
    void* ptr = ct;
    U16* tableU16 = ( (U16*) ptr) + 2;
    void* FSCTptr = (U32*)ptr + 2;
    FSE144_symbolCompressionTransform* symbolTT = (FSE144_symbolCompressionTransform*) FSCTptr;

    /* header */
    tableU16[-2] = (U16) 0;
    tableU16[-1] = (U16) symbolValue;

    /* Build table */
    tableU16[0] = 0;
    tableU16[1] = 0;   /* just in case */

    /* Build Symbol Transformation Table */
    symbolTT[symbolValue].deltaNbBits = 0;
    symbolTT[symbolValue].deltaFindState = 0;

    return 0;
}


static size_t FSE144_compress_usingCTable_generic (void* dst, size_t dstSize,
                           const void* src, size_t srcSize,
                           const FSE144_CTable* ct, const unsigned fast)
{
    const BYTE* const istart = (const BYTE*) src;
    const BYTE* const iend = istart + srcSize;
    const BYTE* ip=iend;

    BIT144_CStream_t bitC;
    FSE144_CState_t CState1, CState2;

    /* init */
    if (srcSize <= 2) return 0;
    { size_t const initError = BIT144_initCStream(&bitC, dst, dstSize);
      if (FSE144_isError(initError)) return 0; /* not enough space available to write a bitstream */ }

#define FSE144_FLUSHBITS(s)  (fast ? BIT144_flushBitsFast(s) : BIT144_flushBits(s))

    if (srcSize & 1) {
        FSE144_initCState2(&CState1, ct, *--ip);
        FSE144_initCState2(&CState2, ct, *--ip);
        FSE144_encodeSymbol(&bitC, &CState1, *--ip);
        FSE144_FLUSHBITS(&bitC);
    } else {
        FSE144_initCState2(&CState2, ct, *--ip);
        FSE144_initCState2(&CState1, ct, *--ip);
    }

    /* join to mod 4 */
    srcSize -= 2;
    if ((sizeof(bitC.bitContainer)*8 > FSE144_MAX_TABLELOG*4+7 ) && (srcSize & 2)) {  /* test bit 2 */
        FSE144_encodeSymbol(&bitC, &CState2, *--ip);
        FSE144_encodeSymbol(&bitC, &CState1, *--ip);
        FSE144_FLUSHBITS(&bitC);
    }

    /* 2 or 4 encoding per loop */
    while ( ip>istart ) {

        FSE144_encodeSymbol(&bitC, &CState2, *--ip);

        if (sizeof(bitC.bitContainer)*8 < FSE144_MAX_TABLELOG*2+7 )   /* this test must be static */
            FSE144_FLUSHBITS(&bitC);

        FSE144_encodeSymbol(&bitC, &CState1, *--ip);

        if (sizeof(bitC.bitContainer)*8 > FSE144_MAX_TABLELOG*4+7 ) {  /* this test must be static */
            FSE144_encodeSymbol(&bitC, &CState2, *--ip);
            FSE144_encodeSymbol(&bitC, &CState1, *--ip);
        }

        FSE144_FLUSHBITS(&bitC);
    }

    FSE144_flushCState(&bitC, &CState2);
    FSE144_flushCState(&bitC, &CState1);
    return BIT144_closeCStream(&bitC);
}

size_t FSE144_compress_usingCTable (void* dst, size_t dstSize,
                           const void* src, size_t srcSize,
                           const FSE144_CTable* ct)
{
    unsigned const fast = (dstSize >= FSE144_BLOCKBOUND(srcSize));

    if (fast)
        return FSE144_compress_usingCTable_generic(dst, dstSize, src, srcSize, ct, 1);
    else
        return FSE144_compress_usingCTable_generic(dst, dstSize, src, srcSize, ct, 0);
}


size_t FSE144_compressBound(size_t size) { return FSE144_COMPRESSBOUND(size); }

#define CHECK_V_F(e, f) size_t const e = f; if (ERR144_isError(e)) return e
#define CHECK_F(f)   { CHECK_V_F(_var_err__, f); }

/* FSE144_compress_wksp() :
 * Same as FSE144_compress2(), but using an externally allocated scratch buffer (`workSpace`).
 * `wkspSize` size must be `(1<<tableLog)`.
 */
size_t FSE144_compress_wksp (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + dstSize;

    unsigned count[FSE144_MAX_SYMBOL_VALUE+1];
    S16   norm[FSE144_MAX_SYMBOL_VALUE+1];
    FSE144_CTable* CTable = (FSE144_CTable*)workSpace;
    size_t const CTableSize = FSE144_CTABLE_SIZE_U32(tableLog, maxSymbolValue);
    void* scratchBuffer = (void*)(CTable + CTableSize);
    size_t const scratchBufferSize = wkspSize - (CTableSize * sizeof(FSE144_CTable));

    /* init conditions */
    if (wkspSize < FSE144_WKSP_SIZE_U32(tableLog, maxSymbolValue)) return ERROR(tableLog_tooLarge);
    if (srcSize <= 1) return 0;  /* Not compressible */
    if (!maxSymbolValue) maxSymbolValue = FSE144_MAX_SYMBOL_VALUE;
    if (!tableLog) tableLog = FSE144_DEFAULT_TABLELOG;

    /* Scan input and build symbol stats */
    {   CHECK_V_F(maxCount, HIST144_count_wksp(count, &maxSymbolValue, src, srcSize, scratchBuffer, scratchBufferSize) );
        if (maxCount == srcSize) return 1;   /* only a single symbol in src : rle */
        if (maxCount == 1) return 0;         /* each symbol present maximum once => not compressible */
        if (maxCount < (srcSize >> 7)) return 0;   /* Heuristic : not compressible enough */
    }

    tableLog = FSE144_optimalTableLog(tableLog, srcSize, maxSymbolValue);
    CHECK_F( FSE144_normalizeCount(norm, tableLog, count, srcSize, maxSymbolValue) );

    /* Write table description header */
    {   CHECK_V_F(nc_err, FSE144_writeNCount(op, oend-op, norm, maxSymbolValue, tableLog) );
        op += nc_err;
    }

    /* Compress */
    CHECK_F( FSE144_buildCTable_wksp(CTable, norm, maxSymbolValue, tableLog, scratchBuffer, scratchBufferSize) );
    {   CHECK_V_F(cSize, FSE144_compress_usingCTable(op, oend - op, src, srcSize, CTable) );
        if (cSize == 0) return 0;   /* not enough space for compressed data */
        op += cSize;
    }

    /* check compressibility */
    if ( (size_t)(op-ostart) >= srcSize-1 ) return 0;

    return op-ostart;
}

typedef struct {
    FSE144_CTable CTable_max[FSE144_CTABLE_SIZE_U32(FSE144_MAX_TABLELOG, FSE144_MAX_SYMBOL_VALUE)];
    BYTE scratchBuffer[1 << FSE144_MAX_TABLELOG];
} fseWkspMax_t;

size_t FSE144_compress2 (void* dst, size_t dstCapacity, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog)
{
    fseWkspMax_t scratchBuffer;
    DEBUG_STATIC_ASSERT(sizeof(scratchBuffer) >= FSE144_WKSP_SIZE_U32(FSE144_MAX_TABLELOG, FSE144_MAX_SYMBOL_VALUE));   /* compilation failures here means scratchBuffer is not large enough */
    if (tableLog > FSE144_MAX_TABLELOG) return ERROR(tableLog_tooLarge);
    return FSE144_compress_wksp(dst, dstCapacity, src, srcSize, maxSymbolValue, tableLog, &scratchBuffer, sizeof(scratchBuffer));
}

size_t FSE144_compress (void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return FSE144_compress2(dst, dstCapacity, src, srcSize, FSE144_MAX_SYMBOL_VALUE, FSE144_DEFAULT_TABLELOG);
}


#endif   /* FSE144_COMMONDEFS_ONLY */
