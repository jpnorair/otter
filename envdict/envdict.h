/* Copyright 2017, JP Norair
 *
 * Licensed under the OpenTag License, Version 1.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.indigresso.com/wiki/doku.php?id=opentag:license_1_0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef envdict_h
#define envdict_h


#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ENVDICT_err     = -1,
    ENVDICT_uint    = 0,
    ENVDICT_int,
    ENVDICT_float,
    ENVDICT_string,
    ENVDICT_TERMINUS
} ENVDICT_type;


typedef union {
    void*           ptr;
    unsigned int    uint;
    double          fpt;
} envdata_u;

/// Handle for envdict.
///@note I don't usually like have handles/objects that resolve as voids, but
///      in this case I don't want to expose a struct that may change wildly
///      in the very near future, as different dict/hash implementations are
///      evaluated.
typedef void* envdict_t;



envdict_t envdict_new(void);
int envdict_deinit(envdict_t dict);

ENVDICT_type envdict_get(envdict_t dict, const char* key, envdata_u* valdata);

int envdict_set(envdict_t dict, const char* key, ENVDICT_type valtype, envdata_u valdata);

int envdict_del(envdict_t dict, const char* key);


#endif /* envdict_t */
