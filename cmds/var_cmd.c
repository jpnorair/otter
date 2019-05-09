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

// Local Headers
#include "cmdutils.h"

#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_cfg.h"
//#include "test.h"


// HBuilder libraries



// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


/*
#include <uthash.h>

typedef enum {
    VAR_Binary = 0,
    VAR_String,
    VAR_Int,
    VAR_Float,
} VAR_Type;

typedef union {
    void* pointer;
    int64_t integer;
    double number;
} var_union;

struct varstruct {
    char name[16];
    int id;
    
    size_t size;
    VAR_Type type;
    var_union val;
    
    UT_hash_handle hh;
};

typedef struct {
    size_t size;
    int    id_master;
    struct varstruct* base;
} otvar_t;

typedef void* otvar_handle_t;





static struct varstruct* sub_findvar(otvar_t* otvar, const char* name) {
    struct varstruct* item;
    struct varstruct* base;
    
    base = otvar->base;
    //HASH_FIND_STR(otvar->base, name, item);
    HASH_FIND_STR(base, name, item);
    return item;
}


int otvar_init(otvar_handle_t* handle) {
    otvar_t* otvar;
    
    if (handle == NULL) {
        return -1;
    }
    
    otvar = malloc(sizeof(otvar_t));
    if (otvar == NULL) {
        return -2;
    }
    
    otvar->size         = 0;
    otvar->id_master    = 0;
    otvar->base         = NULL;
    *handle             = otvar;
    return 0;
}


void otvar_deinit(otvar_handle_t handle) {
    struct varstruct* tmp;
    struct varstruct* item;
    
    if (handle != NULL) {
        struct varstruct* vartab = ((otvar_t*)handle)->base;
        
        HASH_ITER(hh, vartab, item, tmp) {
            HASH_DEL(vartab, item);         // delete item (vartab advances to next)
            if (item->size != 0) {
                free(item->val.pointer);
            }
            free(item);
        }
        
        free(handle);
    }
}


int otvar_del(otvar_handle_t handle, const char* varname) {
    struct varstruct* input;
    struct varstruct* output;
    otvar_t* otvar;
    int dels = 0;
    
    if (handle == NULL) {
        return -1;
    }
    
    otvar = handle;
    if (otvar->base == NULL) {
        return -2;
    }
    
    input = sub_findvar(otvar, varname);
    if (input != NULL) {
        HASH_DEL(input, output);
        if (output->size != 0) {
            free(output->val.pointer);
        }
        free(output);
        dels++;
    }
    
    return dels;
}


int otvar_add(otvar_handle_t handle, const char* varname, VAR_Type type, ...) {
    otvar_t* otvar;
    int dels = 0;
    
    if (handle == NULL) {
        return -1;
    }
    
    otvar = handle;
    if (otvar->base == NULL) {
        return -2;
    }
    
    return 0;
}
*/



int cmd_var(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
//    char* end;
//    char* mark;
//    int output_sz;
//    dict_item_t* var;
//    int rc;
//    int lim = dstmax;
    

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
/*
    ///1. Search for equal sign.  If none exists, then print out the value &
    ///   type of the variable.  The end of the variable name is either the
    ///   end of the input or the position of the equal sign.
    mark = strchr((const char*)src, '=');
    end = (mark == NULL) ? &src[*inbytes] : mark;
    
    ///2. Burn whitespace ahead of and behind variable name,
    ///   and stringify var name with end terminator
    while (isspace(*src)) src++;
    while (isspace(end[-1])) end--;
    *end = 0;
    
    ///3. Validate the variable name.
    ///   - restricted to: (0-9), (A-Z), (a-z), (_)
    {   char* cursor = (char*)src;
        while (*cursor != 0) {
            if (((*cursor >= '0') && (*cursor <= '9'))
            ||  ((*cursor >= 'A') && (*cursor >= 'Z'))
            ||  ((*cursor >= 'a') && (*cursor >= 'z'))
            ||  ((*cursor == '_'))) {
                cursor++;
            }
            else {
                rc = -3;
                strcpy((char*)dst, "Variable name not valid");
                goto cmd_var_END;
            }
        }
    }
    
    ///4. Case of printing-out variable value (mark == NULL)
    if (mark == NULL) {
        var = hash_search(src, *inbytes);
        if (var == NULL) {
            strcpy((char*)dst, "Variable not found");
            rc = -4;
            goto cmd_var_END;
        }

        switch (var->type) {
        default:
            rc = -5;
            strcpy((char*)dst, "Variable has unknown type");
            goto cmd_var_END;
            
        case DICT_Binary:
            if (var->size > 0) {
                output_sz = sprintf((char*)dst, "%s = ", (char*)src);
                lim      -= output_sz;
                dst      += output_sz;
                if (lim <= 0) {
                    goto cmd_var_OVERRUN;
                }
                lim = (lim/2 - 1);
                lim = (var->size > lim) ? lim : var->size;
                output_sz = cmdutils_uint8_to_hexstr((char*)dst, src, lim);
            }
            break;
        case DICT_String:
            output_sz = snprintf((char*)dst, lim, "%s = %s", (char*)src, *(char**)var->data.pointer);
            break;
        case DICT_Int:
            output_sz = snprintf((char*)dst, lim, "%s = %i", (char*)src, var->data.integer);
            break;
        case DICT_Float:
            output_sz = snprintf((char*)dst, lim, "%s = %lf", (char*)src, var->data.floater);
            break;
        }
        
        dterm_send_cmdmsg(dth, "var", (char*)dst);
        rc = 0;
    }
    
    ///5. Case of assigning a variable with a value
    ///   - burn whitespace around boundaries of value
    ///   - todo: bintexify (bintex library needs work)
    ///   - Determine if input is int, float, string.
    else {
        
    }
    
*/
    return 0;
}



