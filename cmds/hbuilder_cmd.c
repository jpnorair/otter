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
    char printbuf[80];
    char* errdesc;
    int rc;
    
    INPUT_SANITIZE_FLAG_EOS(is_eos);
    
    //fprintf(stderr, "hbuilder invoked: %.*s\n", *inbytes, (char*)src);
    
    rc = hbuilder_runcmd(hb_handle, cmd_handle, dst, &bytesout, dstmax, src, inbytes);
    
    if (rc <= 0) {
        if (rc == -2) {
            sprintf(printbuf, "input error on character %zu\n", bytesout);
            errdesc = printbuf;
        }
        else {
            errdesc = NULL;
        }
        dterm_send_error(dth, "hbuilder", rc, 0, errdesc);
    }
    else if (cliopt_isverbose()){
        sprintf(printbuf, "packetized %zu bytes\n", bytesout);
        dterm_send_cmdmsg(dth, "hbuilder", printbuf);
    }

    // This is the data to be sent over the I/O interface
    return (int)bytesout;
}

#   endif

