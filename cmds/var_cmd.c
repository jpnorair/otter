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
#include <bintex.h>
#include <otvar.h>


// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>




int cmd_var(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    char* end;
    char* back;
    char* mark;
    otvar_item_t varitem;
    otvar_handle_t vardict;
    int lim;
    int output_sz;
    int rc;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();

    vardict = ((otter_app_t*)(dth->ext))->vardict;
    lim = (int)dstmax;
    output_sz = 0;
    rc = 0;

    ///1. Search for equal sign.  If none exists, then print out the value &
    ///   type of the variable.  The back of the variable name is either the
    ///   end of the input or the position of the equal sign.
    end  = (char*)&src[*inbytes];
    mark = strchr((const char*)src, '=');
    back = (mark == NULL) ? end : mark;
    
    ///2. Burn whitespace ahead of and behind variable name,
    ///   and stringify var name with end terminator
    while (isspace(*src)) src++;
    while (isspace(*(back-1)) && ((back-1) > (char*)src)) back--;
    *back = 0;
    
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
        varitem = otvar_get_item(vardict, (const char*)src);
        if (varitem == NULL) {
            strcpy((char*)dst, "Variable not found");
            rc = -4;
            goto cmd_var_END;
        }

        switch (otvar_item_type(varitem)) {
            default:
                rc = -5;
                strcpy((char*)dst, "Variable has unknown type");
                goto cmd_var_END;
                
            case VAR_Binary: {
                size_t datasize = otvar_item_size(varitem);
                if (datasize > 0) {
                    output_sz = sprintf((char*)dst, "%s = ", (char*)src);
                    lim      -= output_sz;
                    dst      += output_sz;
                    if (lim > 0) {
                        lim = (lim/2 - 1);
                        lim = (datasize > lim) ? lim : (int)datasize;
                        output_sz += cmdutils_uint8_to_hexstr((char*)dst, src, lim);
                    }
                }
            } break;
            
            case VAR_String: {
                const char* itemstr = otvar_item_string(varitem);
                if (itemstr != NULL) {
                    output_sz = snprintf((char*)dst, lim, "%s = %s", (char*)src, itemstr);
                }
            } break;
            
            case VAR_Int: {
                output_sz = snprintf((char*)dst, lim, "%s = %lli", (char*)src, otvar_item_integer(varitem));
            } break;
            
            case VAR_Float: {
                output_sz = snprintf((char*)dst, lim, "%s = %lf", (char*)src, otvar_item_number(varitem));
            } break;
        }
        
        dterm_send_cmdmsg(dth, "var", (char*)dst);
        rc = 0;
    }
    
    ///5. Case of assigning a variable with a value
    else {
        // Move mark past equal sign, and burn whitespace around entry
        mark++;
        while (isspace(*mark)) mark++;
        while (isspace(*(end-1)) && ((end-1) > mark)) end--;
        *end = 0;

        // Zero length: implicit delete operation
        // input = #: explicit delete
        // input = some other string: explicit add
        if ((*mark == 0) || (strcmp(mark, "#") == 0)) {
            ///@todo only be able to delete user vars
            //otvar_del(vardict, (const char*)src);
        }
        else {
            long long integer;
            double floater;
            char* endptr;

            integer = strtoll(mark, &endptr, 0);
            if (*endptr == '\0') {
                otvar_add(vardict, (const char*)src, VAR_Int, integer);
                goto cmd_var_END;
            }

            floater = strtod(mark, &endptr);
            if (*endptr == '\0') {
                otvar_add(vardict, (const char*)src, VAR_Float, floater);
                goto cmd_var_END;
            }

            output_sz = bintex_ss((unsigned char*)mark, dst, (int)dstmax);
            if (output_sz < 0) {
                ///@todo coordinate base error (-256) with code for bintex_ss
                rc = -256 + output_sz;
                goto cmd_var_END;
            }
            if (*mark == '\"') {
                dst[output_sz] = 0;
                otvar_add(vardict, (const char*)src, VAR_String, dst);
            }
            else {
                otvar_add(vardict, (const char*)src, VAR_Binary, output_sz, dst);
            }
        }
    }

    cmd_var_END:    
    return rc;
}



