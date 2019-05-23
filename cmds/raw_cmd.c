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

#include <bintex.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>



#define HOME_PATH_MAX           1024
char home_path[HOME_PATH_MAX]   = "~/";
int home_path_len               = 2;



///@todo make separate commands for file & string based input
// Raw Protocol Entry: This is implemented fully and it takes a Bintex
// expression as input, with no special keywords or arguments.
int cmd_raw(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    const char* filepath;
    FILE*       fp;
    int         bytesout;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    // Consider absolute path
    if (src[0] == '/') {
        filepath = (const char*)src;
    }
    
    // Build path from relative path, co-opting packet buffer temporarily
    else {
        int bytes_left;
        bytes_left = (HOME_PATH_MAX - home_path_len);
        strncat((char*)home_path, (char*)src, bytes_left);
        filepath = (const char*)home_path;
    }
    
    // Try opening the file.  If it doesn't work, then assume the input is a
    // bintex string and not a file string
    fp = fopen(filepath, "r");
    if (fp != NULL) {
        bytesout = bintex_fs(fp, (unsigned char*)dst, (int)dstmax);
        fclose(fp);
    }
    else {
        bytesout = bintex_ss((unsigned char*)src, (unsigned char*)dst, (int)dstmax);
    }
    
    // Undo whatever was done to the home_path
    home_path[home_path_len] = 0;
    
    ///@todo convert the character number into a line and character number
    if (bytesout < 0) {
        sprintf((char*)dst, "input error on character %i", -bytesout);
    }
    else if ((bytesout > 0) && cliopt_isverbose() && (dth->intf->type == INTF_interactive)) {
        char printbuf[80];
        snprintf(printbuf, 80, "packetizing %d bytes (max=%zu)", bytesout, dstmax);
        dterm_send_cmdmsg(dth, "raw", printbuf);
    }

    return bytesout;
}
