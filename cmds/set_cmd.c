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

#include "envvar.h"

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






int cmd_set(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
///@todo This will do setting of Otter Env Variables.
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    fprintf(stderr, "\"set\" command not yet implemented\n");

    return 0;
}


int cmd_sethome(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
///@todo make this a recipient for "set HOME" or simply remove it.  Must be aligned
///      with environment variable module.

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    if (*inbytes >= 1023) {
        dterm_puts(dth->dt, "Error: supplied home-path is too long, must be < 1023 chars.\n");
    }
    else {
        ///@todo just make this call "set HOME"
    
//        strcpy(home_path, (char*)src);
//        if (home_path[*inbytes]  != '/') {
//            home_path[*inbytes]   = '/';
//            home_path[*inbytes+1] = 0;
//        }
//        home_path_len = *inbytes+1;
    }
    
    return 0;
}


