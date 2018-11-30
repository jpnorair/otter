/* Copyright 2014, JP Norair
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

// Otter Headers
#include "cliopt.h"
#include "cmds.h"
#include "envvar.h"
#include "otter_cfg.h"
//#include "test.h"

// HB Headers/Libraries
#include <judy.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>




typedef struct {
    void* judy;
    size_t maxkey;
} envdict_t;




int envvar_init(void** handle, size_t max_namelen) {
    envdict_t* envdict;

    if (handle == NULL) {
        return -1;
    }
    if (max_namelen < 2) {
        return -2;
    }
    
    envdict = (void*)malloc(sizeof(envdict_t));
    if (envdict == NULL) {
        return -3;
    }
    
    *handle         = envdict;
    envdict->maxkey = max_namelen;
    envdict->judy   = judy_open((unsigned int)(4*envdict->maxkey), 0);
    if (envdict->judy == NULL) {
        free(envdict);
        return -4;
    }

    return 0;
}


void envvar_free(void* handle) {
    envdict_t* envdict;

    if (handle != NULL) {
        envdict = handle;
        
        ///@todo delete all memory malloced from judy pointers
        
        
        

        if (envdict->judy != NULL) {
            judy_close(envdict->judy);
        }
    }
}


int envvar_new(void* handle, const char* name, envdict_type_enum type, size_t size, void* data) {
    size_t total_size;
    uint32_t* jdata;
    unsigned char name_val[16];
    JudySlot* newcell;
    envdict_t* envdict;
    int rc = 0;
    
    if (handle == NULL) {
        return -1;
    }
    envdict = handle;
    
    switch (type) {
        case ENVDICT_string:
            size = strlen( (char*)data ) + 1;
            break;
        case ENVDICT_int:
            size *= sizeof(int);
            break;
        case ENVDICT_float:
            size *= sizeof(double);
            break;
        case ENVDICT_double:
            size *= sizeof(double);
            break;
       default:
            break;
    }
    
    total_size = (sizeof(uint32_t)*2) + size;
    if (total_size > (JUDY_seg - 8)) {
        rc = -1;
        goto cmd_envvar_new_END;
    }
    
    memset(name_val, 0, 16);
    strncpy((char*)name_val, name, 15);
    newcell = judy_cell(envdict->judy, name_val, (unsigned int)strlen((const char*)name_val));
    if (newcell == NULL) {
        rc = -2;
        goto cmd_envvar_new_END;
    }
    
    jdata = judy_data(envdict->judy, (unsigned int)total_size);
    if (jdata == NULL) {
        judy_del(envdict->judy);
        rc = -3;
        goto cmd_envvar_new_END;
    }
    
    *newcell = (JudySlot)jdata;
    jdata[0] = (uint32_t)type;
    jdata[1] = (uint32_t)size;
    if (size != 0) {
        memcpy(&jdata[2], data, size);
    }
    
    cmd_envvar_new_END:
    return rc;
}


int envvar_del(void* handle, const char* name) {
    unsigned int name_len;
    void* val;
    int rc;
    envdict_t* envdict;
    
    if (handle == NULL) {
        return -1;
    }
    envdict = handle;
    
    name_len = (unsigned int)strlen(name);
    if (name_len > 15) {
        name_len = 15;
    }
    
    val = judy_slot(envdict->judy, (const unsigned char*)name, name_len);
    
    if (val != NULL) {
        judy_del(envdict->judy);
        rc = 0;
    }
    else {
        rc = -1;
    }
    
    return rc;
}


int envvar_get(void* handle, size_t* varsize, void** vardata, const char* name) {
    unsigned int name_len;
    uint32_t* envdata;
    JudySlot* val;
    int rc;
    envdict_t* envdict;
    
    if ((handle == NULL) || (varsize == NULL) || (vardata == NULL)) {
        return -1;
    }
    
    envdict = handle;
    
    name_len = (unsigned int)strlen(name);
    if (name_len > 15) {
        name_len = 15;
    }
    
    val = judy_slot(envdict->judy, (const unsigned char*)name, name_len);
    
    if (val != NULL) {
        envdata     = (uint32_t*)*val;
        rc          = (envdict_type_enum)envdata[0];
        *varsize    = (size_t)envdata[1];
        *vardata    = &envdata[2];
    }
    else {
        rc = -2;
    }
    
    return rc;
}


int envvar_getint(void* handle, const char* name) {
    size_t varsize;
    void* vardata;
    int rc;
    
    rc = envvar_get(handle, &varsize, &vardata, name);
    if (rc < 0) {
        rc = 0;
    }
    else {
        rc = ((int*)vardata)[0];
    }
    
    return rc;
}


