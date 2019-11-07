/* ******************************************************************
   FSE : Finite State Entropy codec
   Public Prototypes declaration
   Copyright (C) 2013-2016, Yann Collet.

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
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */

#if defined (__cplusplus)
extern "C" {
#endif

#ifndef FSE144_H
#define FSE144_H


/*-*****************************************
*  Dependencies
******************************************/
#include <stddef.h>    /* size_t, ptrdiff_t */


/*-*****************************************
*  FSE144_PUBLIC_API : control library symbols visibility
******************************************/
#if defined(FSE144_DLL144_EXPORT) && (FSE144_DLL144_EXPORT==1) && defined(__GNUC__) && (__GNUC__ >= 4)
#  define FSE144_PUBLIC_API __attribute__ ((visibility ("default")))
#elif defined(FSE144_DLL144_EXPORT) && (FSE144_DLL144_EXPORT==1)   /* Visual expected */
#  define FSE144_PUBLIC_API __declspec(dllexport)
#elif defined(FSE144_DLL144_IMPORT) && (FSE144_DLL144_IMPORT==1)
#  define FSE144_PUBLIC_API __declspec(dllimport) /* It isn't required but allows to generate better code, saving a function pointer load from the IAT and an indirect jump.*/
#else
#  define FSE144_PUBLIC_API
#endif

/*------   Version   ------*/
#define FSE144_VERSION_MAJOR    0
#define FSE144_VERSION_MINOR    9
#define FSE144_VERSION_RELEASE  0

#define FSE144_LIB_VERSION FSE144_VERSION_MAJOR.FSE144_VERSION_MINOR.FSE144_VERSION_RELEASE
#define FSE144_QUOTE(str) #str
#define FSE144_EXPAND_AND_QUOTE(str) FSE144_QUOTE(str)
#define FSE144_VERSION_STRING FSE144_EXPAND_AND_QUOTE(FSE144_LIB_VERSION)

#define FSE144_VERSION_NUMBER  (FSE144_VERSION_MAJOR *100*100 + FSE144_VERSION_MINOR *100 + FSE144_VERSION_RELEASE)
FSE144_PUBLIC_API unsigned FSE144_versionNumber(void);   /**< library version number; to be used when checking dll version */


/*-****************************************
*  FSE simple functions
******************************************/
/*! FSE144_compress() :
    Compress content of buffer 'src', of size 'srcSize', into destination buffer 'dst'.
    'dst' buffer must be already allocated. Compression runs faster is dstCapacity >= FSE144_compressBound(srcSize).
    @return : size of compressed data (<= dstCapacity).
    Special values : if return == 0, srcData is not compressible => Nothing is stored within dst !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression instead.
                     if FSE144_isError(return), compression failed (more details using FSE144_getErrorName())
*/
FSE144_PUBLIC_API size_t FSE144_compress(void* dst, size_t dstCapacity,
                             const void* src, size_t srcSize);

/*! FSE144_decompress():
    Decompress FSE data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated destination buffer 'dst', of size 'dstCapacity'.
    @return : size of regenerated data (<= maxDstSize),
              or an error code, which can be tested using FSE144_isError() .

    ** Important ** : FSE144_decompress() does not decompress non-compressible nor RLE data !!!
    Why ? : making this distinction requires a header.
    Header management is intentionally delegated to the user layer, which can better manage special cases.
*/
FSE144_PUBLIC_API size_t FSE144_decompress(void* dst,  size_t dstCapacity,
                               const void* cSrc, size_t cSrcSize);


/*-*****************************************
*  Tool functions
******************************************/
FSE144_PUBLIC_API size_t FSE144_compressBound(size_t size);       /* maximum compressed size */

/* Error Management */
FSE144_PUBLIC_API unsigned    FSE144_isError(size_t code);        /* tells if a return value is an error code */
FSE144_PUBLIC_API const char* FSE144_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/*-*****************************************
*  FSE advanced functions
******************************************/
/*! FSE144_compress2() :
    Same as FSE144_compress(), but allows the selection of 'maxSymbolValue' and 'tableLog'
    Both parameters can be defined as '0' to mean : use default value
    @return : size of compressed data
    Special values : if return == 0, srcData is not compressible => Nothing is stored within cSrc !!!
                     if return == 1, srcData is a single byte symbol * srcSize times. Use RLE compression.
                     if FSE144_isError(return), it's an error code.
*/
FSE144_PUBLIC_API size_t FSE144_compress2 (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog);


/*-*****************************************
*  FSE detailed API
******************************************/
/*!
FSE144_compress() does the following:
1. count symbol occurrence from source[] into table count[] (see hist.h)
2. normalize counters so that sum(count[]) == Power_of_2 (2^tableLog)
3. save normalized counters to memory buffer using writeNCount()
4. build encoding table 'CTable' from normalized counters
5. encode the data stream using encoding table 'CTable'

FSE144_decompress() does the following:
1. read normalized counters with readNCount()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using external method.
*/

/* *** COMPRESSION *** */

/*! FSE144_optimalTableLog():
    dynamically downsize 'tableLog' when conditions are met.
    It saves CPU time, by using smaller tables, while preserving or even improving compression ratio.
    @return : recommended tableLog (necessarily <= 'maxTableLog') */
FSE144_PUBLIC_API unsigned FSE144_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);

/*! FSE144_normalizeCount():
    normalize counts so that sum(count[]) == Power_of_2 (2^tableLog)
    'normalizedCounter' is a table of short, of minimum size (maxSymbolValue+1).
    @return : tableLog,
              or an errorCode, which can be tested using FSE144_isError() */
FSE144_PUBLIC_API size_t FSE144_normalizeCount(short* normalizedCounter, unsigned tableLog,
                    const unsigned* count, size_t srcSize, unsigned maxSymbolValue);

/*! FSE144_NCountWriteBound():
    Provides the maximum possible size of an FSE normalized table, given 'maxSymbolValue' and 'tableLog'.
    Typically useful for allocation purpose. */
FSE144_PUBLIC_API size_t FSE144_NCountWriteBound(unsigned maxSymbolValue, unsigned tableLog);

/*! FSE144_writeNCount():
    Compactly save 'normalizedCounter' into 'buffer'.
    @return : size of the compressed table,
              or an errorCode, which can be tested using FSE144_isError(). */
FSE144_PUBLIC_API size_t FSE144_writeNCount (void* buffer, size_t bufferSize,
                                 const short* normalizedCounter,
                                 unsigned maxSymbolValue, unsigned tableLog);

/*! Constructor and Destructor of FSE144_CTable.
    Note that FSE144_CTable size depends on 'tableLog' and 'maxSymbolValue' */
typedef unsigned FSE144_CTable;   /* don't allocate that. It's only meant to be more restrictive than void* */
FSE144_PUBLIC_API FSE144_CTable* FSE144_createCTable (unsigned maxSymbolValue, unsigned tableLog);
FSE144_PUBLIC_API void        FSE144_freeCTable (FSE144_CTable* ct);

/*! FSE144_buildCTable():
    Builds `ct`, which must be already allocated, using FSE144_createCTable().
    @return : 0, or an errorCode, which can be tested using FSE144_isError() */
FSE144_PUBLIC_API size_t FSE144_buildCTable(FSE144_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! FSE144_compress_usingCTable():
    Compress `src` using `ct` into `dst` which must be already allocated.
    @return : size of compressed data (<= `dstCapacity`),
              or 0 if compressed data could not fit into `dst`,
              or an errorCode, which can be tested using FSE144_isError() */
FSE144_PUBLIC_API size_t FSE144_compress_usingCTable (void* dst, size_t dstCapacity, const void* src, size_t srcSize, const FSE144_CTable* ct);

/*!
Tutorial :
----------
The first step is to count all symbols. FSE144_count() does this job very fast.
Result will be saved into 'count', a table of unsigned int, which must be already allocated, and have 'maxSymbolValuePtr[0]+1' cells.
'src' is a table of bytes of size 'srcSize'. All values within 'src' MUST be <= maxSymbolValuePtr[0]
maxSymbolValuePtr[0] will be updated, with its real value (necessarily <= original value)
FSE144_count() will return the number of occurrence of the most frequent symbol.
This can be used to know if there is a single symbol within 'src', and to quickly evaluate its compressibility.
If there is an error, the function will return an ErrorCode (which can be tested using FSE144_isError()).

The next step is to normalize the frequencies.
FSE144_normalizeCount() will ensure that sum of frequencies is == 2 ^'tableLog'.
It also guarantees a minimum of 1 to any Symbol with frequency >= 1.
You can use 'tableLog'==0 to mean "use default tableLog value".
If you are unsure of which tableLog value to use, you can ask FSE144_optimalTableLog(),
which will provide the optimal valid tableLog given sourceSize, maxSymbolValue, and a user-defined maximum (0 means "default").

The result of FSE144_normalizeCount() will be saved into a table,
called 'normalizedCounter', which is a table of signed short.
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValue+1' cells.
The return value is tableLog if everything proceeded as expected.
It is 0 if there is a single symbol within distribution.
If there is an error (ex: invalid tableLog value), the function will return an ErrorCode (which can be tested using FSE144_isError()).

'normalizedCounter' can be saved in a compact manner to a memory area using FSE144_writeNCount().
'buffer' must be already allocated.
For guaranteed success, buffer size must be at least FSE144_headerBound().
The result of the function is the number of bytes written into 'buffer'.
If there is an error, the function will return an ErrorCode (which can be tested using FSE144_isError(); ex : buffer size too small).

'normalizedCounter' can then be used to create the compression table 'CTable'.
The space required by 'CTable' must be already allocated, using FSE144_createCTable().
You can then use FSE144_buildCTable() to fill 'CTable'.
If there is an error, both functions will return an ErrorCode (which can be tested using FSE144_isError()).

'CTable' can then be used to compress 'src', with FSE144_compress_usingCTable().
Similar to FSE144_count(), the convention is that 'src' is assumed to be a table of char of size 'srcSize'
The function returns the size of compressed data (without header), necessarily <= `dstCapacity`.
If it returns '0', compressed data could not fit into 'dst'.
If there is an error, the function will return an ErrorCode (which can be tested using FSE144_isError()).
*/


/* *** DECOMPRESSION *** */

/*! FSE144_readNCount():
    Read compactly saved 'normalizedCounter' from 'rBuffer'.
    @return : size read from 'rBuffer',
              or an errorCode, which can be tested using FSE144_isError().
              maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
FSE144_PUBLIC_API size_t FSE144_readNCount (short* normalizedCounter,
                           unsigned* maxSymbolValuePtr, unsigned* tableLogPtr,
                           const void* rBuffer, size_t rBuffSize);

/*! Constructor and Destructor of FSE144_DTable.
    Note that its size depends on 'tableLog' */
typedef unsigned FSE144_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */
FSE144_PUBLIC_API FSE144_DTable* FSE144_createDTable(unsigned tableLog);
FSE144_PUBLIC_API void        FSE144_freeDTable(FSE144_DTable* dt);

/*! FSE144_buildDTable():
    Builds 'dt', which must be already allocated, using FSE144_createDTable().
    return : 0, or an errorCode, which can be tested using FSE144_isError() */
FSE144_PUBLIC_API size_t FSE144_buildDTable (FSE144_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! FSE144_decompress_usingDTable():
    Decompress compressed source `cSrc` of size `cSrcSize` using `dt`
    into `dst` which must be already allocated.
    @return : size of regenerated data (necessarily <= `dstCapacity`),
              or an errorCode, which can be tested using FSE144_isError() */
FSE144_PUBLIC_API size_t FSE144_decompress_usingDTable(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, const FSE144_DTable* dt);

/*!
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by FSE144_readNCount() if it was saved using FSE144_writeNCount().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of signed short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSE144_readNCount() will provide 'tableLog' and 'maxSymbolValue'.
The result of FSE144_readNCount() is the number of bytes read from 'rBuffer'.
Note that 'rBufferSize' must be at least 4 bytes, even if useful information is less than that.
If there is an error, the function will return an error code, which can be tested using FSE144_isError().

The next step is to build the decompression tables 'FSE144_DTable' from 'normalizedCounter'.
This is performed by the function FSE144_buildDTable().
The space required by 'FSE144_DTable' must be already allocated using FSE144_createDTable().
If there is an error, the function will return an error code, which can be tested using FSE144_isError().

`FSE144_DTable` can then be used to decompress `cSrc`, with FSE144_decompress_usingDTable().
`cSrcSize` must be strictly correct, otherwise decompression will fail.
FSE144_decompress_usingDTable() result will tell how many bytes were regenerated (<=`dstCapacity`).
If there is an error, the function will return an error code, which can be tested using FSE144_isError(). (ex: dst buffer too small)
*/

#endif  /* FSE144_H */

#if defined(FSE144_STATIC_LINKING_ONLY) && !defined(FSE144_H_FSE144_STATIC_LINKING_ONLY)
#define FSE144_H_FSE144_STATIC_LINKING_ONLY

/* *** Dependency *** */
#include "bitstream.h"


/* *****************************************
*  Static allocation
*******************************************/
/* FSE buffer bounds */
#define FSE144_NCOUNTBOUND 512
#define FSE144_BLOCKBOUND(size) (size + (size>>7) + 4 /* fse states */ + sizeof(size_t) /* bitContainer */)
#define FSE144_COMPRESSBOUND(size) (FSE144_NCOUNTBOUND + FSE144_BLOCKBOUND(size))   /* Macro version, useful for static allocation */

/* It is possible to statically allocate FSE CTable/DTable as a table of FSE144_CTable/FSE144_DTable using below macros */
#define FSE144_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue)   (1 + (1<<(maxTableLog-1)) + ((maxSymbolValue+1)*2))
#define FSE144_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<maxTableLog))

/* or use the size to malloc() space directly. Pay attention to alignment restrictions though */
#define FSE144_CTABLE_SIZE(maxTableLog, maxSymbolValue)   (FSE144_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) * sizeof(FSE144_CTable))
#define FSE144_DTABLE_SIZE(maxTableLog)                   (FSE144_DTABLE_SIZE_U32(maxTableLog) * sizeof(FSE144_DTable))


/* *****************************************
 *  FSE advanced API
 ***************************************** */

unsigned FSE144_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
/**< same as FSE144_optimalTableLog(), which used `minus==2` */

/* FSE144_compress_wksp() :
 * Same as FSE144_compress2(), but using an externally allocated scratch buffer (`workSpace`).
 * FSE144_WKSP_SIZE_U32() provides the minimum size required for `workSpace` as a table of FSE144_CTable.
 */
#define FSE144_WKSP_SIZE_U32(maxTableLog, maxSymbolValue)   ( FSE144_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) + ((maxTableLog > 12) ? (1 << (maxTableLog - 2)) : 1024) )
size_t FSE144_compress_wksp (void* dst, size_t dstSize, const void* src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);

size_t FSE144_buildCTable_raw (FSE144_CTable* ct, unsigned nbBits);
/**< build a fake FSE144_CTable, designed for a flat distribution, where each symbol uses nbBits */

size_t FSE144_buildCTable_rle (FSE144_CTable* ct, unsigned char symbolValue);
/**< build a fake FSE144_CTable, designed to compress always the same symbolValue */

/* FSE144_buildCTable_wksp() :
 * Same as FSE144_buildCTable(), but using an externally allocated scratch buffer (`workSpace`).
 * `wkspSize` must be >= `(1<<tableLog)`.
 */
size_t FSE144_buildCTable_wksp(FSE144_CTable* ct, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void* workSpace, size_t wkspSize);

size_t FSE144_buildDTable_raw (FSE144_DTable* dt, unsigned nbBits);
/**< build a fake FSE144_DTable, designed to read a flat distribution where each symbol uses nbBits */

size_t FSE144_buildDTable_rle (FSE144_DTable* dt, unsigned char symbolValue);
/**< build a fake FSE144_DTable, designed to always generate the same symbolValue */

size_t FSE144_decompress_wksp(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, FSE144_DTable* workSpace, unsigned maxLog);
/**< same as FSE144_decompress(), using an externally allocated `workSpace` produced with `FSE144_DTABLE_SIZE_U32(maxLog)` */

typedef enum {
   FSE144_repeat_none,  /**< Cannot use the previous table */
   FSE144_repeat_check, /**< Can use the previous table but it must be checked */
   FSE144_repeat_valid  /**< Can use the previous table and it is assumed to be valid */
 } FSE144_repeat;

/* *****************************************
*  FSE symbol compression API
*******************************************/
/*!
   This API consists of small unitary functions, which highly benefit from being inlined.
   Hence their body are included in next section.
*/
typedef struct {
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} FSE144_CState_t;

static void FSE144_initCState(FSE144_CState_t* CStatePtr, const FSE144_CTable* ct);

static void FSE144_encodeSymbol(BIT144_CStream_t* bitC, FSE144_CState_t* CStatePtr, unsigned symbol);

static void FSE144_flushCState(BIT144_CStream_t* bitC, const FSE144_CState_t* CStatePtr);

/**<
These functions are inner components of FSE144_compress_usingCTable().
They allow the creation of custom streams, mixing multiple tables and bit sources.

A key property to keep in mind is that encoding and decoding are done **in reverse direction**.
So the first symbol you will encode is the last you will decode, like a LIFO stack.

You will need a few variables to track your CStream. They are :

FSE144_CTable    ct;         // Provided by FSE144_buildCTable()
BIT144_CStream_t bitStream;  // bitStream tracking structure
FSE144_CState_t  state;      // State tracking structure (can have several)


The first thing to do is to init bitStream and state.
    size_t errorCode = BIT144_initCStream(&bitStream, dstBuffer, maxDstSize);
    FSE144_initCState(&state, ct);

Note that BIT144_initCStream() can produce an error code, so its result should be tested, using FSE144_isError();
You can then encode your input data, byte after byte.
FSE144_encodeSymbol() outputs a maximum of 'tableLog' bits at a time.
Remember decoding will be done in reverse direction.
    FSE144_encodeByte(&bitStream, &state, symbol);

At any time, you can also add any bit sequence.
Note : maximum allowed nbBits is 25, for compatibility with 32-bits decoders
    BIT144_addBits(&bitStream, bitField, nbBits);

The above methods don't commit data to memory, they just store it into local register, for speed.
Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
Writing data to memory is a manual operation, performed by the flushBits function.
    BIT144_flushBits(&bitStream);

Your last FSE encoding operation shall be to flush your last state value(s).
    FSE144_flushState(&bitStream, &state);

Finally, you must close the bitStream.
The function returns the size of CStream in bytes.
If data couldn't fit into dstBuffer, it will return a 0 ( == not compressible)
If there is an error, it returns an errorCode (which can be tested using FSE144_isError()).
    size_t size = BIT144_closeCStream(&bitStream);
*/


/* *****************************************
*  FSE symbol decompression API
*******************************************/
typedef struct {
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} FSE144_DState_t;


static void     FSE144_initDState(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD, const FSE144_DTable* dt);

static unsigned char FSE144_decodeSymbol(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD);

static unsigned FSE144_endOfDState(const FSE144_DState_t* DStatePtr);

/**<
Let's now decompose FSE144_decompress_usingDTable() into its unitary components.
You will decode FSE-encoded symbols from the bitStream,
and also any other bitFields you put in, **in reverse order**.

You will need a few variables to track your bitStream. They are :

BIT144_DStream_t DStream;    // Stream context
FSE144_DState_t  DState;     // State context. Multiple ones are possible
FSE144_DTable*   DTablePtr;  // Decoding table, provided by FSE144_buildDTable()

The first thing to do is to init the bitStream.
    errorCode = BIT144_initDStream(&DStream, srcBuffer, srcSize);

You should then retrieve your initial state(s)
(in reverse flushing order if you have several ones) :
    errorCode = FSE144_initDState(&DState, &DStream, DTablePtr);

You can then decode your data, symbol after symbol.
For information the maximum number of bits read by FSE144_decodeSymbol() is 'tableLog'.
Keep in mind that symbols are decoded in reverse order, like a LIFO stack (last in, first out).
    unsigned char symbol = FSE144_decodeSymbol(&DState, &DStream);

You can retrieve any bitfield you eventually stored into the bitStream (in reverse order)
Note : maximum allowed nbBits is 25, for 32-bits compatibility
    size_t bitField = BIT144_readBits(&DStream, nbBits);

All above operations only read from local register (which size depends on size_t).
Refueling the register from memory is manually performed by the reload method.
    endSignal = FSE144_reloadDStream(&DStream);

BIT144_reloadDStream() result tells if there is still some more data to read from DStream.
BIT144_DStream_unfinished : there is still some data left into the DStream.
BIT144_DStream_endOfBuffer : Dstream reached end of buffer. Its container may no longer be completely filled.
BIT144_DStream_completed : Dstream reached its exact end, corresponding in general to decompression completed.
BIT144_DStream_tooFar : Dstream went too far. Decompression result is corrupted.

When reaching end of buffer (BIT144_DStream_endOfBuffer), progress slowly, notably if you decode multiple symbols per loop,
to properly detect the exact end of stream.
After each decoded symbol, check if DStream is fully consumed using this simple test :
    BIT144_reloadDStream(&DStream) >= BIT144_DStream_completed

When it's done, verify decompression is fully completed, by checking both DStream and the relevant states.
Checking if DStream has reached its end is performed by :
    BIT144_endOfDStream(&DStream);
Check also the states. There might be some symbols left there, if some high probability ones (>50%) are possible.
    FSE144_endOfDState(&DState);
*/


/* *****************************************
*  FSE unsafe API
*******************************************/
static unsigned char FSE144_decodeSymbolFast(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD);
/* faster, but works only if nbBits is always >= 1 (otherwise, result will be corrupted) */


/* *****************************************
*  Implementation of inlined functions
*******************************************/
typedef struct {
    int deltaFindState;
    U32 deltaNbBits;
} FSE144_symbolCompressionTransform; /* total 8 bytes */

MEM_STATIC void FSE144_initCState(FSE144_CState_t* statePtr, const FSE144_CTable* ct)
{
    const void* ptr = ct;
    const U16* u16ptr = (const U16*) ptr;
    const U32 tableLog = MEM_read16(ptr);
    statePtr->value = (ptrdiff_t)1<<tableLog;
    statePtr->stateTable = u16ptr+2;
    statePtr->symbolTT = ct + 1 + (tableLog ? (1<<(tableLog-1)) : 1);
    statePtr->stateLog = tableLog;
}


/*! FSE144_initCState2() :
*   Same as FSE144_initCState(), but the first symbol to include (which will be the last to be read)
*   uses the smallest state value possible, saving the cost of this symbol */
MEM_STATIC void FSE144_initCState2(FSE144_CState_t* statePtr, const FSE144_CTable* ct, U32 symbol)
{
    FSE144_initCState(statePtr, ct);
    {   const FSE144_symbolCompressionTransform symbolTT = ((const FSE144_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
        const U16* stateTable = (const U16*)(statePtr->stateTable);
        U32 nbBitsOut  = (U32)((symbolTT.deltaNbBits + (1<<15)) >> 16);
        statePtr->value = (nbBitsOut << 16) - symbolTT.deltaNbBits;
        statePtr->value = stateTable[(statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
    }
}

MEM_STATIC void FSE144_encodeSymbol(BIT144_CStream_t* bitC, FSE144_CState_t* statePtr, unsigned symbol)
{
    FSE144_symbolCompressionTransform const symbolTT = ((const FSE144_symbolCompressionTransform*)(statePtr->symbolTT))[symbol];
    const U16* const stateTable = (const U16*)(statePtr->stateTable);
    U32 const nbBitsOut  = (U32)((statePtr->value + symbolTT.deltaNbBits) >> 16);
    BIT144_addBits(bitC, statePtr->value, nbBitsOut);
    statePtr->value = stateTable[ (statePtr->value >> nbBitsOut) + symbolTT.deltaFindState];
}

MEM_STATIC void FSE144_flushCState(BIT144_CStream_t* bitC, const FSE144_CState_t* statePtr)
{
    BIT144_addBits(bitC, statePtr->value, statePtr->stateLog);
    BIT144_flushBits(bitC);
}


/* FSE144_getMaxNbBits() :
 * Approximate maximum cost of a symbol, in bits.
 * Fractional get rounded up (i.e : a symbol with a normalized frequency of 3 gives the same result as a frequency of 2)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
MEM_STATIC U32 FSE144_getMaxNbBits(const void* symbolTTPtr, U32 symbolValue)
{
    const FSE144_symbolCompressionTransform* symbolTT = (const FSE144_symbolCompressionTransform*) symbolTTPtr;
    return (symbolTT[symbolValue].deltaNbBits + ((1<<16)-1)) >> 16;
}

/* FSE144_bitCost() :
 * Approximate symbol cost, as fractional value, using fixed-point format (accuracyLog fractional bits)
 * note 1 : assume symbolValue is valid (<= maxSymbolValue)
 * note 2 : if freq[symbolValue]==0, @return a fake cost of tableLog+1 bits */
MEM_STATIC U32 FSE144_bitCost(const void* symbolTTPtr, U32 tableLog, U32 symbolValue, U32 accuracyLog)
{
    const FSE144_symbolCompressionTransform* symbolTT = (const FSE144_symbolCompressionTransform*) symbolTTPtr;
    U32 const minNbBits = symbolTT[symbolValue].deltaNbBits >> 16;
    U32 const threshold = (minNbBits+1) << 16;
    assert(tableLog < 16);
    assert(accuracyLog < 31-tableLog);  /* ensure enough room for renormalization double shift */
    {   U32 const tableSize = 1 << tableLog;
        U32 const deltaFromThreshold = threshold - (symbolTT[symbolValue].deltaNbBits + tableSize);
        U32 const normalizedDeltaFromThreshold = (deltaFromThreshold << accuracyLog) >> tableLog;   /* linear interpolation (very approximate) */
        U32 const bitMultiplier = 1 << accuracyLog;
        assert(symbolTT[symbolValue].deltaNbBits + tableSize <= threshold);
        assert(normalizedDeltaFromThreshold <= bitMultiplier);
        return (minNbBits+1)*bitMultiplier - normalizedDeltaFromThreshold;
    }
}


/* ======    Decompression    ====== */

typedef struct {
    U16 tableLog;
    U16 fastMode;
} FSE144_DTableHeader;   /* sizeof U32 */

typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} FSE144_decode_t;   /* size == U32 */

MEM_STATIC void FSE144_initDState(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD, const FSE144_DTable* dt)
{
    const void* ptr = dt;
    const FSE144_DTableHeader* const DTableH = (const FSE144_DTableHeader*)ptr;
    DStatePtr->state = BIT144_readBits(bitD, DTableH->tableLog);
    BIT144_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

MEM_STATIC BYTE FSE144_peekSymbol(const FSE144_DState_t* DStatePtr)
{
    FSE144_decode_t const DInfo = ((const FSE144_decode_t*)(DStatePtr->table))[DStatePtr->state];
    return DInfo.symbol;
}

MEM_STATIC void FSE144_updateState(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD)
{
    FSE144_decode_t const DInfo = ((const FSE144_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BIT144_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;
}

MEM_STATIC BYTE FSE144_decodeSymbol(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD)
{
    FSE144_decode_t const DInfo = ((const FSE144_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BIT144_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/*! FSE144_decodeSymbolFast() :
    unsafe, only works if no symbol has a probability > 50% */
MEM_STATIC BYTE FSE144_decodeSymbolFast(FSE144_DState_t* DStatePtr, BIT144_DStream_t* bitD)
{
    FSE144_decode_t const DInfo = ((const FSE144_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BIT144_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

MEM_STATIC unsigned FSE144_endOfDState(const FSE144_DState_t* DStatePtr)
{
    return DStatePtr->state == 0;
}



#ifndef FSE144_COMMONDEFS_ONLY

/* **************************************************************
*  Tuning parameters
****************************************************************/
/*!MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#ifndef FSE144_MAX_MEMORY_USAGE
#  define FSE144_MAX_MEMORY_USAGE 14
#endif
#ifndef FSE144_DEFAULT_MEMORY_USAGE
#  define FSE144_DEFAULT_MEMORY_USAGE 13
#endif

/*!FSE144_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#ifndef FSE144_MAX_SYMBOL_VALUE
#  define FSE144_MAX_SYMBOL_VALUE 255
#endif

/* **************************************************************
*  template functions type & suffix
****************************************************************/
#define FSE144_FUNCTION_TYPE BYTE
#define FSE144_FUNCTION_EXTENSION
#define FSE144_DECODE_TYPE FSE144_decode_t


#endif   /* !FSE144_COMMONDEFS_ONLY */


/* ***************************************************************
*  Constants
*****************************************************************/
#define FSE144_MAX_TABLELOG  (FSE144_MAX_MEMORY_USAGE-2)
#define FSE144_MAX_TABLESIZE (1U<<FSE144_MAX_TABLELOG)
#define FSE144_MAXTABLESIZE_MASK (FSE144_MAX_TABLESIZE-1)
#define FSE144_DEFAULT_TABLELOG (FSE144_DEFAULT_MEMORY_USAGE-2)
#define FSE144_MIN_TABLELOG 5

#define FSE144_TABLELOG_ABSOLUTE_MAX 15
#if FSE144_MAX_TABLELOG > FSE144_TABLELOG_ABSOLUTE_MAX
#  error "FSE144_MAX_TABLELOG > FSE144_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define FSE144_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)


#endif /* FSE144_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif
