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


#include "envdict.h"

// Hash table implementation
// UTHash is purely macro driven
#include "uthash.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>



typedef struct {
    const char* key;
    ENVDICT_type    valtype;
    envdata_u       val;
    UT_hash_handle  hh;         // makes this structure hashable in UTHash
} hash_t;



envdict_t envdict_init(void) {
    envdict_t handle;
    
    handle = (void*)malloc(sizeof(hash_t*));
    
    return handle;
}



int envdict_deinit(envdict_t dict) {
/// 1. remove all items from library
/// 2. free handle itself

    if (dict != NULL) {
        hash_t* this;
        hash_t* tmp;
        hash_t* table = (hash_t*)dict;
        
        HASH_ITER(hh, table, this, tmp) {
            HASH_DEL(table, this);
            free(this);
        }
        
        free(table);
    }

    return 0;
}


ENVDICT_type envdict_get(envdict_t dict, const char* key, envdata_u* valdata) {
    hash_t* table           = (hash_t*)dict;
    hash_t* s               = NULL;
    ENVDICT_type ret_type   = ENVDICT_err;
    
    HASH_FIND_STR(table, key, s);
    if (s != NULL) {
        if ((s->valtype >= ENVDICT_uint) && (s->valtype < ENVDICT_TERMINUS)) {
            ret_type = s->valtype;
            *valdata = s->val;
        }
    }

    return ret_type;
}



int envdict_set(envdict_t dict, const char* key, ENVDICT_type valtype, envdata_u valdata) {
    hash_t* table   = (hash_t*)dict;
    hash_t* new;
    int rc = 0;
    
    /// Quick check to see if type is valid before adding
    if ((valtype < ENVDICT_uint) || (valtype >= ENVDICT_TERMINUS)) {
        rc = -1;
        goto envdict_add_END;
    }
    
    /// Delete old entry if the key exists.
    /// We don't care about the return value of this function.
    envdict_del(dict, key);
    
    /// Create a new key-value item.  Exit with error on failed malloc
    new = malloc(sizeof(hash_t));
    if (new == NULL) {
        rc = -2;
        goto envdict_add_END;
    }
    
    ///@note not sure if need to do some error handling from the Hash macro.
    new->key        = key;
    new->valtype    = valtype;
    new->val        = valdata;
    HASH_ADD_KEYPTR(hh, table, new->key, strlen(new->key), new );  
    
    envdict_add_END:
    return rc;
}


int envdict_del(envdict_t dict, const char* key) {
    hash_t* table   = (hash_t*)dict;
    hash_t* s;
    
    HASH_FIND_STR(table, key, s);
    if (s == NULL) {
        return -1;
    }

    HASH_DEL(table, s);
    free(s);

    return 0;
}



