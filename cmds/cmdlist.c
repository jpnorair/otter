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


int cmd_cmdlist(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int bytes_out;
    char cmdprint[1024];

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    bytes_out = cmdtab_list(dth->cmdtab, cmdprint, 1024);
    dterm_puts(dth->dt, "Commands available:\n");
    dterm_puts(dth->dt, cmdprint);
    
    return 0;
}
