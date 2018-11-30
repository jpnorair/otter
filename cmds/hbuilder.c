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

#include "otter_cfg.h"
#if OTTER_FEATURE(HBUILDER)

// Local Headers
#include "cmds.h"
#include "cmdutils.h"
#include "envvar.h"
#include "cliopt.h"
#include "dterm.h"
//#include "test.h"


// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <hbuilder.h>



int cmdext_hbuilder(void* hb_handle, void* cmd_handle, dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    bool is_eos     = false;
    size_t bytesout = 0;
    int rc;
    
//    if (dt == NULL) {
//#       if defined(__HBUILDER__)
//        hbcc_init();
//#       endif
//        return 0;
//    }
    
    INPUT_SANITIZE_FLAG_EOS(is_eos);
    
    //fprintf(stderr, "hbuilder invoked: %.*s\n", *inbytes, (char*)src);
    

    rc = hbuilder_runcmd(hb_handle, cmd_handle, dst, &bytesout, dstmax, src, inbytes);
    
    if (rc < 0) {
        if (cliopt_getformat() == FORMAT_Default) {
            dterm_printf(dth->dt, "HBuilder Command Error (code %d)\n", rc);
            if (rc == -2) {
                dterm_printf(dth->dt, "--> Input Error on character %zu\n", bytesout);
            }
        }
        else if (cliopt_getformat() == FORMAT_Json) {
            dterm_printf(dth->dt, "{\"cmd\":\"%s\", \"err\":%i}", "hb", rc);
        }
    }
    else if ((rc > 0) && cliopt_isverbose()) {
        if (cliopt_getformat() == FORMAT_Default) {
            fprintf(stdout, "--> HBuilder packetizing %zu bytes\n", bytesout);
        }
        else if (cliopt_getformat() == FORMAT_Json) {
            dterm_printf(dth->dt, "{\"cmd\":\"%s\", \"msg\":\"HBuilder packetizing %zu bytes\"}", "hb", bytesout);
        }
    }

    return rc;
}

#   endif


//    fprintf(stderr, "hbuilder invoked, but not supported.\n--> %s\n", src);
//    rc = -2;
