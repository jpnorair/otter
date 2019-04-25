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



// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>






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



