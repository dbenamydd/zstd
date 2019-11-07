/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#ifndef ZSTD144_DDICT_H
#define ZSTD144_DDICT_H

/*-*******************************************************
 *  Dependencies
 *********************************************************/
#include <stddef.h>   /* size_t */
#include "zstd.h"     /* ZSTD144_DDict, and several public functions */


/*-*******************************************************
 *  Interface
 *********************************************************/

/* note: several prototypes are already published in `zstd.h` :
 * ZSTD144_createDDict()
 * ZSTD144_createDDict_byReference()
 * ZSTD144_createDDict_advanced()
 * ZSTD144_freeDDict()
 * ZSTD144_initStaticDDict()
 * ZSTD144_sizeof_DDict()
 * ZSTD144_estimateDDictSize()
 * ZSTD144_getDictID_fromDict()
 */

const void* ZSTD144_DDict_dictContent(const ZSTD144_DDict* ddict);
size_t ZSTD144_DDict_dictSize(const ZSTD144_DDict* ddict);

void ZSTD144_copyDDictParameters(ZSTD144_DCtx* dctx, const ZSTD144_DDict* ddict);



#endif /* ZSTD144_DDICT_H */
