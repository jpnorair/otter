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
#include "cmds.h"
#include "cmdutils.h"
#include "cliopt.h"

#include "dterm.h"
#include "otter_cfg.h"

// HBuilder Libraries
#include <argtable3.h>
#include <bintex.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>


int cmd_loopx(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int argc;
    char** argv;
    int rc;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(&argv, "loopx", (char*)src, (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
    }
    else {
        FILE* fp = NULL;
        bool fp_isbtx = true;
        int datbytes;
        uint8_t* loopbytes;
        uint8_t* loopcurs;
        int bsize_val   = 128;
        int timeout_val = 1000;
        int retries_val = 3;
        int fd_out;
        
        struct arg_lit* file    = arg_lit0("-f","file",                 "Use file for loop input, instead of line input");
        struct arg_int* bsize   = arg_int0("-b","bsize","bytes",        "Segment binary inputs into blocks of specified size");
        struct arg_int* timeout = arg_int0("-t","timeout","ms",         "Loop response timeout in ms.  Default 1000.");
        struct arg_int* retries = arg_int0("-r","retries","count",      "Number of retries until exit loop.  Default 3.");
        struct arg_str* loopcmd = arg_str1(NULL,NULL,"loop cmd",        "Command put in quotes (\"\") to loop");
        struct arg_str* loopdat = arg_str1(NULL,NULL,"loop data",       "Loop data as bintex or file, per -f flag.");
        struct arg_end* end     = arg_end(7);
        void* argtable[]        = { file, bsize, loopcmd, loopdat, end };
        
        rc = cmdutils_argcheck(argtable, end, argc, argv);
        if (rc != 0) {
            goto cmd_loopx_TERM;
        }
        
        if (bsize->count > 0) {
            bsize_val = bsize->ival[0];
        }
        
        if (file->count > 0) {
            fp = fopen(loopdat->sval[0], "r");
            if (fp == NULL) {
                rc = -3;
                goto cmd_loopx_TERM;
            }
            
            // Get size of file
            if (fseek(fp, 0, SEEK_END) != 0) {
                rc = -4;
                fclose(fp);
                goto cmd_loopx_TERM;
            }
            datbytes = (int)ftell(fp);
            rewind(fp);
            
            fp_isbtx = (bool)strcmp(&(loopdat->sval[0][strlen(loopdat->sval[0])-4]), ".bin");
            
            if (fp_isbtx) {
                datbytes /= 2;
            }
            
            loopbytes = malloc(datbytes);
            if (loopbytes == NULL) {
                rc = -5;
                fclose(fp);
                goto cmd_loopx_TERM;
            }
            
            if (fp_isbtx) {
                datbytes = bintex_fs(fp, loopbytes, datbytes);
                if (datbytes < 0) {
                    rc = -6;
                    fclose(fp);
                    goto cmd_loopx_TERM;
                }
            }
        }
        else {
            datbytes = (int)strlen(loopdat->sval[0]) / 2;
            loopbytes = malloc(datbytes);
            if (loopbytes == NULL) {
                rc = -5;
                goto cmd_loopx_TERM;
            }
        
            datbytes = bintex_ss((unsigned char*)loopdat->sval[0], (unsigned char*)loopbytes, (int)datbytes);
        }
        
        loopcurs = loopbytes;
        
        /// Loop setup
        /// 1.  Squelch the dterm so the parser doesn't output anything during
        ///     the command looping.
        /// 2.  Set up a subscriber, which lets the parser post some data to a
        ///     buffer which the command looper can access.
        /// 3.  unlock the dtwrite mutex to allow parser thread to operate.
        fd_out = dterm_squelch(dth->dt);
        if (fd_out > 0) {
            rc = -6;
            goto cmd_loopx_TERM;
        }
        
        
        
        pthread_mutex_unlock(dth->dtwrite_mutex);
        
        /// --- Loopy command part
        
        
        /// ---
        
        /// Relock the dtwrite mutex to block the parser thread while this
        /// command finishes.
        pthread_mutex_lock(dth->dtwrite_mutex);
        dterm_unsquelch(dth->dt);
        
        free(loopbytes);
        
        cmd_loopx_TERM:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    }
    
    cmdutils_freeargv(argv);
    return rc;
}

