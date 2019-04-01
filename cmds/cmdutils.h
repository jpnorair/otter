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
#include "cliopt.h"
#include "cmds.h"

// HBuilder Libs
#include <argtable3.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


int cmdutils_uint8_to_hexstr(char* dst, uint8_t* src, size_t src_bytes);


int cmdutils_hexstr_to_uint8(uint8_t* dst, const char* src);


int cmdutils_base64_to_uint8(uint8_t* dst, const char* src);


uint8_t* cmdutils_markstring(uint8_t** psrc, int* search_limit, int string_limit);


uint8_t* cmdutils_goto_eol(uint8_t* src);


int cmdutils_parsestring(void* ctx, char*** pargv, const char* cmdname, char* src, size_t src_limit);


void cmdutils_freeargv(void* ctx, char** argv);


int cmdutils_argcheck(void* argtable, struct arg_end* end, int argc, char** argv);


#define INPUT_SANITIZE() do { \
    if ((src == NULL) || (dst == NULL)) {   \
        *inbytes = 0;                                       \
        return -1;                                          \
    }                                                       \
    {   uint8_t* eol    = cmdutils_goto_eol(src);                    \
        *inbytes        = (int)(eol-src);                   \
        *eol   = 0;                                         \
    }                                                       \
} while(0)

#define INPUT_SANITIZE_FLAG_EOS(IS_EOS) do { \
    if ((src == NULL) || (dst == NULL)) {   \
        *inbytes = 0;                                       \
        return -1;                                          \
    }                                                       \
    {   uint8_t* eol    = cmdutils_goto_eol(src);                    \
        *inbytes        = (int)(eol-src);                   \
        IS_EOS          = (bool)(*eol == 0);                \
        *eol   = 0;                                         \
    }                                                       \
} while(0)


