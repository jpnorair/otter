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

#include <talloc.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

///@todo add context arguments to all functions


int cmdutils_uint8_to_hexstr(char* dst, uint8_t* src, size_t src_bytes) {
    const char convert[] = "0123456789ABCDEF";
    char* start = dst;
    
    while (src_bytes-- != 0) {
        *dst++ = convert[*src >> 4];
        *dst++ = convert[*src & 0x0f];
        src++;
    }
    *dst = 0;
    
    return (int)(dst - start);
}

int cmdutils_hexstr_to_uint8(uint8_t* dst, const char* src) {
    size_t chars;
    int bytes;
    
    chars   = strlen(src);
    chars >>= 1;    // round down to nearest even
    bytes   = (int)chars;
    
    while (chars != 0) {
        char c;
        uint8_t a, b;
        chars--;
        
        c = *src++;
        if      (c >= '0' && c <= '9')  a = c - '0';
        else if (c >= 'A' && c <= 'F')  a = 10 + c - 'A';
        else if (c >= 'a' && c <= 'f')  a = 10 + c - 'a';
        else    a = 0;
        
        c = *src++;
        if      (c >= '0' && c <= '9')  b = c - '0';
        else if (c >= 'A' && c <= 'F')  b = 10 + c - 'A';
        else if (c >= 'a' && c <= 'f')  b = 10 + c - 'a';
        else    b = 0;
        
        *dst++ = (a << 4) + b;
    }
    
    return bytes;
}


int cmdutils_base64_to_uint8(uint8_t* dst, const char* src) {
///@todo build this function on a rainy day -- probably reference a BASE64 lib.
    return 0;
}



uint8_t* cmdutils_markstring(uint8_t** psrc, int* search_limit, int string_limit) {
    size_t      code_len;
    size_t      code_max;
    uint8_t*    cursor;
    uint8_t*    front;
    
    /// 1. Set search limit on the string to mark within the source string
    code_max    = (*search_limit < string_limit) ? *search_limit : string_limit; 
    front       = *psrc;
    
    /// 2. Go past whitespace in the front of the source string if there is any.
    ///    This updates the position of the source string itself, so the caller
    ///    must save the position of the source string if it wishes to go back.
    while (isspace(**psrc)) { 
        (*psrc)++; 
    }
    
    /// 3. Put a Null Terminator where whitespace is found after the marked
    ///    string.
    for (code_len=0, cursor=*psrc; (code_len < code_max); code_len++, cursor++) {
        if (isspace(*cursor)) {
            *cursor = 0;
            cursor++;
            break;
        }
    }
    
    /// 4. Go past any whitespace after the cursor position, and update cursor.
    while (isspace(*cursor)) { 
        cursor++; 
    }
    
    /// 5. reduce the message limit counter given the bytes we've gone past.
    *search_limit -= (cursor - front);
    
    return cursor;
}



uint8_t* cmdutils_goto_eol(uint8_t* src) {
    uint8_t* end = src;
    
    while ((*end != 0) && (*end != '\n')) {
        end++;
    }
    
    return end;
}


int cmdutils_parsestring(void* ctx, char*** pargv, const char* cmdname, char* src, size_t src_limit) {
    char* cursor;
    int argc = 0;
    int remdata;
    int paren, brack, quote;
    bool ws_lead;

    cursor = src;

    /// Zero out whitespace that's not protected
    /// Protections:
    /// - Quotes "..."
    /// - Brackets [...]
    /// - Parentheses (...)
    ws_lead = true;
    remdata = (int)src_limit;
    paren   = 0;
    brack   = 0;
    quote   = 0;
    while ((*cursor != 0) && (remdata != 0)) {
        switch (*cursor) {
            case '\"':  quote = (quote == 0);   break;
            case '[':   brack++;                break;
            case '(':   paren++;                break;
            case ']':   brack -= (brack > 0);   break;
            case ')':   paren -= (paren > 0);   break;
            default:
                if ((quote==0) && (brack==0) && (paren==0)) {
                    if (isspace(*cursor)) {
                        *cursor = 0;
                    }
                }
                break;
        }
        if ((ws_lead == true) && (*cursor != 0)) {
            argc++;
        }
        ws_lead = (bool)(*cursor == 0);
        cursor++;
        remdata--;
    }
    
    /// If 'cmdname' is NULL: the command name is included in the source string
    /// If 'cmdname' is not NULL: the command name is supplied in 'cmdname'
    if (cmdname != NULL) {
        argc++;
    }
    
    /// If argc is not 0:
    /// - allocate argv
    /// - populate argv array with pointers to first chars after zeros.
    if (argc != 0) {
        if (ctx == NULL) {
            *pargv = calloc(sizeof(char*), argc);
        }
        else {
            *pargv = talloc_zero_size(ctx, sizeof(char*) * argc);
        }
        if (*pargv == NULL) {
            argc = -2;
        }
        else {
            int i;
            if (cmdname != NULL) {
                (*pargv)[0] = (char*)cmdname;
                i = 1;
            }
            else {
                i = 0;
            }
            
            // Bypass leading whitespace (now zeros)
            // Fill argv[i]
            // Bypass trailing non-whitespace
            cursor = src;
            for (; i<argc; i++) {
                for (; *cursor==0; cursor++);
                (*pargv)[i] = cursor;
                for (; *cursor!=0; cursor++);
            }
        }
    }

    return argc;
}

void cmdutils_freeargv(void* ctx, char** argv) {
    if (ctx == NULL) {
        free(argv);
    }
    else {
        talloc_free(argv);
    }
}



int cmdutils_argcheck(void* argtable, struct arg_end* end, int argc, char** argv) {
    int rc = 0;
    
    if (arg_nullcheck(argtable) != 0) {
        rc = -1;
    }
    if ((argc <= 1) || (arg_parse(argc, argv, argtable) > 0)) {
        arg_print_errors(stderr, end, argv[0]);
        ///@todo print help
        rc = -2;
    }

    return rc;
}

