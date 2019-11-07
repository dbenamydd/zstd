/*
   xxHash - Extremely Fast Hash algorithm
   Header File
   Copyright (C) 2012-2016, Yann Collet.

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
   - xxHash source repository : https://github.com/Cyan4973/xxHash
*/

/* Notice extracted from xxHash homepage :

xxHash is an extremely fast Hash algorithm, running at RAM speed limits.
It also successfully passes all tests from the SMHasher suite.

Comparison (single thread, Windows Seven 32 bits, using SMHasher on a Core 2 Duo @3GHz)

Name            Speed       Q.Score   Author
xxHash          5.4 GB/s     10
CrapWow         3.2 GB/s      2       Andrew
MumurHash 3a    2.7 GB/s     10       Austin Appleby
SpookyHash      2.0 GB/s     10       Bob Jenkins
SBox            1.4 GB/s      9       Bret Mulvey
Lookup3         1.2 GB/s      9       Bob Jenkins
SuperFastHash   1.2 GB/s      1       Paul Hsieh
CityHash64      1.05 GB/s    10       Pike & Alakuijala
FNV             0.55 GB/s     5       Fowler, Noll, Vo
CRC32           0.43 GB/s     9
MD5-32          0.33 GB/s    10       Ronald L. Rivest
SHA1-32         0.28 GB/s    10

Q.Score is a measure of quality of the hash function.
It depends on successfully passing SMHasher test set.
10 is a perfect score.

A 64-bits version, named XXH_3264, is available since r35.
It offers much better speed, but for 64-bits applications only.
Name     Speed on 64 bits    Speed on 32 bits
XXH_3264       13.8 GB/s            1.9 GB/s
XXH_3232        6.8 GB/s            6.0 GB/s
*/

#if defined (__cplusplus)
extern "C" {
#endif

#ifndef XXH_32ASH_H_5627135585666179
#define XXH_32ASH_H_5627135585666179 1


/* ****************************
*  Definitions
******************************/
#include <stddef.h>   /* size_t */
typedef enum { XXH_32_OK=0, XXH_32_ERROR } XXH_32_errorcode;


/* ****************************
*  API modifier
******************************/
/** XXH_32_PRIVATE_API
*   This is useful if you want to include xxhash functions in `static` mode
*   in order to inline them, and remove their symbol from the public list.
*   Methodology :
*     #define XXH_32_PRIVATE_API
*     #include "xxhash.h"
*   `xxhash.c` is automatically included.
*   It's not useful to compile and link it as a separate module anymore.
*/
#ifdef XXH_32_PRIVATE_API
#  ifndef XXH_32_STATIC_LINKING_ONLY
#    define XXH_32_STATIC_LINKING_ONLY
#  endif
#  if defined(__GNUC__)
#    define XXH_32_PUBLIC_API static __inline __attribute__((unused))
#  elif defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#    define XXH_32_PUBLIC_API static inline
#  elif defined(_MSC_VER)
#    define XXH_32_PUBLIC_API static __inline
#  else
#    define XXH_32_PUBLIC_API static   /* this version may generate warnings for unused static functions; disable the relevant warning */
#  endif
#else
#  define XXH_32_PUBLIC_API   /* do nothing */
#endif /* XXH_32_PRIVATE_API */

/*!XXH_32_NAMESPACE, aka Namespace Emulation :

If you want to include _and expose_ xxHash functions from within your own library,
but also want to avoid symbol collisions with another library which also includes xxHash,

you can use XXH_32_NAMESPACE, to automatically prefix any public symbol from xxhash library
with the value of XXH_32_NAMESPACE (so avoid to keep it NULL and avoid numeric values).

Note that no change is required within the calling program as long as it includes `xxhash.h` :
regular symbol name will be automatically translated by this header.
*/
#ifdef XXH_32_NAMESPACE
#  define XXH_32_CAT(A,B) A##B
#  define XXH_32_NAME2(A,B) XXH_32_CAT(A,B)
#  define XXH_3232 XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232)
#  define XXH_3264 XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264)
#  define XXH_32_versionNumber XXH_32_NAME2(XXH_32_NAMESPACE, XXH_32_versionNumber)
#  define XXH_3232_createState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_createState)
#  define XXH_3264_createState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_createState)
#  define XXH_3232_freeState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_freeState)
#  define XXH_3264_freeState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_freeState)
#  define XXH_3232_reset XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_reset)
#  define XXH_3264_reset XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_reset)
#  define XXH_3232_update XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_update)
#  define XXH_3264_update XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_update)
#  define XXH_3232_digest XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_digest)
#  define XXH_3264_digest XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_digest)
#  define XXH_3232_copyState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_copyState)
#  define XXH_3264_copyState XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_copyState)
#  define XXH_3232_canonicalFromHash XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_canonicalFromHash)
#  define XXH_3264_canonicalFromHash XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_canonicalFromHash)
#  define XXH_3232_hashFromCanonical XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3232_hashFromCanonical)
#  define XXH_3264_hashFromCanonical XXH_32_NAME2(XXH_32_NAMESPACE, XXH_3264_hashFromCanonical)
#endif


/* *************************************
*  Version
***************************************/
#define XXH_32_VERSION_MAJOR    0
#define XXH_32_VERSION_MINOR    6
#define XXH_32_VERSION_RELEASE  2
#define XXH_32_VERSION_NUMBER  (XXH_32_VERSION_MAJOR *100*100 + XXH_32_VERSION_MINOR *100 + XXH_32_VERSION_RELEASE)
XXH_32_PUBLIC_API unsigned XXH_32_versionNumber (void);


/* ****************************
*  Simple Hash Functions
******************************/
typedef unsigned int       XXH_3232_hash_t;
typedef unsigned long long XXH_3264_hash_t;

XXH_32_PUBLIC_API XXH_3232_hash_t XXH_3232 (const void* input, size_t length, unsigned int seed);
XXH_32_PUBLIC_API XXH_3264_hash_t XXH_3264 (const void* input, size_t length, unsigned long long seed);

/*!
XXH_3232() :
    Calculate the 32-bits hash of sequence "length" bytes stored at memory address "input".
    The memory between input & input+length must be valid (allocated and read-accessible).
    "seed" can be used to alter the result predictably.
    Speed on Core 2 Duo @ 3 GHz (single thread, SMHasher benchmark) : 5.4 GB/s
XXH_3264() :
    Calculate the 64-bits hash of sequence of length "len" stored at memory address "input".
    "seed" can be used to alter the result predictably.
    This function runs 2x faster on 64-bits systems, but slower on 32-bits systems (see benchmark).
*/


/* ****************************
*  Streaming Hash Functions
******************************/
typedef struct XXH_3232_state_s XXH_3232_state_t;   /* incomplete type */
typedef struct XXH_3264_state_s XXH_3264_state_t;   /* incomplete type */

/*! State allocation, compatible with dynamic libraries */

XXH_32_PUBLIC_API XXH_3232_state_t* XXH_3232_createState(void);
XXH_32_PUBLIC_API XXH_32_errorcode  XXH_3232_freeState(XXH_3232_state_t* statePtr);

XXH_32_PUBLIC_API XXH_3264_state_t* XXH_3264_createState(void);
XXH_32_PUBLIC_API XXH_32_errorcode  XXH_3264_freeState(XXH_3264_state_t* statePtr);


/* hash streaming */

XXH_32_PUBLIC_API XXH_32_errorcode XXH_3232_reset  (XXH_3232_state_t* statePtr, unsigned int seed);
XXH_32_PUBLIC_API XXH_32_errorcode XXH_3232_update (XXH_3232_state_t* statePtr, const void* input, size_t length);
XXH_32_PUBLIC_API XXH_3232_hash_t  XXH_3232_digest (const XXH_3232_state_t* statePtr);

XXH_32_PUBLIC_API XXH_32_errorcode XXH_3264_reset  (XXH_3264_state_t* statePtr, unsigned long long seed);
XXH_32_PUBLIC_API XXH_32_errorcode XXH_3264_update (XXH_3264_state_t* statePtr, const void* input, size_t length);
XXH_32_PUBLIC_API XXH_3264_hash_t  XXH_3264_digest (const XXH_3264_state_t* statePtr);

/*
These functions generate the xxHash of an input provided in multiple segments.
Note that, for small input, they are slower than single-call functions, due to state management.
For small input, prefer `XXH_3232()` and `XXH_3264()` .

XXH_32 state must first be allocated, using XXH_32*_createState() .

Start a new hash by initializing state with a seed, using XXH_32*_reset().

Then, feed the hash state by calling XXH_32*_update() as many times as necessary.
Obviously, input must be allocated and read accessible.
The function returns an error code, with 0 meaning OK, and any other value meaning there is an error.

Finally, a hash value can be produced anytime, by using XXH_32*_digest().
This function returns the nn-bits hash as an int or long long.

It's still possible to continue inserting input into the hash state after a digest,
and generate some new hashes later on, by calling again XXH_32*_digest().

When done, free XXH_32 state space if it was allocated dynamically.
*/


/* **************************
*  Utils
****************************/
#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))   /* ! C99 */
#  define restrict   /* disable restrict */
#endif

XXH_32_PUBLIC_API void XXH_3232_copyState(XXH_3232_state_t* restrict dst_state, const XXH_3232_state_t* restrict src_state);
XXH_32_PUBLIC_API void XXH_3264_copyState(XXH_3264_state_t* restrict dst_state, const XXH_3264_state_t* restrict src_state);


/* **************************
*  Canonical representation
****************************/
/* Default result type for XXH_32 functions are primitive unsigned 32 and 64 bits.
*  The canonical representation uses human-readable write convention, aka big-endian (large digits first).
*  These functions allow transformation of hash result into and from its canonical format.
*  This way, hash values can be written into a file / memory, and remain comparable on different systems and programs.
*/
typedef struct { unsigned char digest[4]; } XXH_3232_canonical_t;
typedef struct { unsigned char digest[8]; } XXH_3264_canonical_t;

XXH_32_PUBLIC_API void XXH_3232_canonicalFromHash(XXH_3232_canonical_t* dst, XXH_3232_hash_t hash);
XXH_32_PUBLIC_API void XXH_3264_canonicalFromHash(XXH_3264_canonical_t* dst, XXH_3264_hash_t hash);

XXH_32_PUBLIC_API XXH_3232_hash_t XXH_3232_hashFromCanonical(const XXH_3232_canonical_t* src);
XXH_32_PUBLIC_API XXH_3264_hash_t XXH_3264_hashFromCanonical(const XXH_3264_canonical_t* src);

#endif /* XXH_32ASH_H_5627135585666179 */



/* ================================================================================================
   This section contains definitions which are not guaranteed to remain stable.
   They may change in future versions, becoming incompatible with a different version of the library.
   They shall only be used with static linking.
   Never use these definitions in association with dynamic linking !
=================================================================================================== */
#if defined(XXH_32_STATIC_LINKING_ONLY) && !defined(XXH_32_STATIC_H_3543687687345)
#define XXH_32_STATIC_H_3543687687345

/* These definitions are only meant to allow allocation of XXH_32 state
   statically, on stack, or in a struct for example.
   Do not use members directly. */

   struct XXH_3232_state_s {
       unsigned total_len_32;
       unsigned large_len;
       unsigned v1;
       unsigned v2;
       unsigned v3;
       unsigned v4;
       unsigned mem32[4];   /* buffer defined as U32 for alignment */
       unsigned memsize;
       unsigned reserved;   /* never read nor write, will be removed in a future version */
   };   /* typedef'd to XXH_3232_state_t */

   struct XXH_3264_state_s {
       unsigned long long total_len;
       unsigned long long v1;
       unsigned long long v2;
       unsigned long long v3;
       unsigned long long v4;
       unsigned long long mem64[4];   /* buffer defined as U64 for alignment */
       unsigned memsize;
       unsigned reserved[2];          /* never read nor write, will be removed in a future version */
   };   /* typedef'd to XXH_3264_state_t */


#  ifdef XXH_32_PRIVATE_API
#    include "xxhash.c"   /* include xxhash functions as `static`, for inlining */
#  endif

#endif /* XXH_32_STATIC_LINKING_ONLY && XXH_32_STATIC_H_3543687687345 */


#if defined (__cplusplus)
}
#endif
