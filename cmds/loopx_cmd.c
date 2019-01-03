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
#include "cmd_api.h"
#include "cmdutils.h"
#include "cliopt.h"
#include "dterm.h"
#include "otter_cfg.h"
#include "subscribers.h"

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
#include <errno.h>



///@todo command line processing should be a function used in all cases of
/// command line processing (mainly, in dterm.c as well as here).  These three
/// subroutines should be consolidated into some helper function lib.

static void sub_str_sanitize(char* str, size_t max) {
    while ((*str != 0) && (max != 0)) {
        if (*str == '\r') {
            *str = '\n';
        }
        str++;
        max--;
    }
}

static size_t sub_str_mark(char* str, size_t max) {
    char* s1 = str;
    while ((*str!=0) && (*str!='\n') && (max!=0)) {
        max--;
        str++;
    }
    if (*str=='\n') *str = 0;
    
    return (str - s1);
}


const cmdtab_item_t* sub_cmdacquire(char** cmddata, dterm_handle_t* dth, char* cmdline) {
    /// Get each line from the pipe.
    const cmdtab_item_t* cmdptr;
    char cmdname[32];
    int cmdlen;

    cmdlen = (int)strlen(cmdline);
    sub_str_sanitize(cmdline, (size_t)cmdlen);
    
    // Burn whitespace ahead of command.
    // Then determine length until newline, or null.
    // then search/get command in list.
    while (isspace(*cmdline)) { cmdline++; --cmdlen; }
    cmdlen  = (int)sub_str_mark(cmdline, (size_t)cmdlen);
    cmdlen  = cmd_getname(cmdname, cmdline, sizeof(cmdname));
    cmdptr  = cmd_search(dth->cmdtab, cmdname);
    
    *cmddata = cmdline+cmdlen;
    return cmdptr;
}

int sub_cmdrun(dterm_handle_t* dth, const cmdtab_item_t* cmdptr, char* cmddata, uint8_t* dst, size_t dstmax) {
    /// Get each line from the pipe.
    int bytesout;
    int bytesin = 0;
    bytesout = cmd_run(cmdptr, dth, dst, &bytesin, (uint8_t*)cmddata, dstmax);
    if (bytesout > 0) {
        int list_size;
        pthread_mutex_lock(dth->tlist_mutex);
        list_size = pktlist_add_tx(&dth->endpoint, dth->tlist, dst, bytesout);
        pthread_mutex_unlock(dth->tlist_mutex);
        if (list_size > 0) {
            pthread_cond_signal(dth->tlist_cond);
        }
    }
    
    return bytesout;
}







int cmd_xloop(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int argc;
    char** argv;
    int rc;
    
    int datbytes;
    int dat_i;
    uint8_t* loopbytes;
    int bsize_val;
    int timeout_val;
    int retries_val;
    int retries_cnt;
    
    int fd_out;
    char* cmdline;
    const cmdtab_item_t* cmdptr;
    subscr_t subscription;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    /// Stage 1: Command Line Protocol Extraction
    rc = 0;
    argc = cmdutils_parsestring(&argv, "xloop", (char*)src, (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
        goto cmd_loopx_EXIT;
    }
    {   struct arg_lit* file    = arg_lit0("-f","file",                 "Use file for loop input, instead of line input");
        struct arg_int* bsize   = arg_int0("-b","bsize","bytes",        "Segment binary inputs into blocks of specified size");
        struct arg_int* timeout = arg_int0("-t","timeout","ms",         "Loop response timeout in ms.  Default 1000.");
        struct arg_int* retries = arg_int0("-r","retries","count",      "Number of retries until exit loop.  Default 3.");
        struct arg_str* loopcmd = arg_str1(NULL,NULL,"loop cmd",        "Command put in quotes (\"\") to loop");
        struct arg_str* loopdat = arg_str1(NULL,NULL,"loop data",       "Loop data as bintex or file, per -f flag.");
        struct arg_end* end     = arg_end(7);
        void* argtable[]        = { file, bsize, loopcmd, loopdat, end };
        
        rc = cmdutils_argcheck(argtable, end, argc, argv);
        if (rc != 0) {
            goto cmd_loopx_FREEARGS;
        }
        
        bsize_val   = (bsize->count > 0)    ? bsize->ival[0]    : 128;
        timeout_val = (timeout->count > 0)  ? timeout->ival[0]  : 1000;
        retries_val = (retries->count > 0)  ? retries->ival[0]  : 3;
        retries_cnt = retries_val;
        
        /// Copy File Data into binary buffer
        if (file->count > 0) {
            FILE* fp;
            bool fp_isbtx = true;
            
            fp = fopen(loopdat->sval[0], "r");
            if (fp == NULL) {
                rc = -3;
                goto cmd_loopx_FREEARGS;
            }
            
            // Get size of file
            if (fseek(fp, 0, SEEK_END) != 0) {
                rc = -4;
                fclose(fp);
                goto cmd_loopx_FREEARGS;
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
                goto cmd_loopx_FREEARGS;
            }
            
            if (fp_isbtx) {
                datbytes = bintex_fs(fp, loopbytes, datbytes);
                if (datbytes < 0) {
                    rc = -6;
                    fclose(fp);
                    goto cmd_loopx_FREEARGS;
                }
            }
        }
        else {
            datbytes = (int)strlen(loopdat->sval[0]) / 2;
            loopbytes = malloc(datbytes);
            if (loopbytes == NULL) {
                rc = -5;
                goto cmd_loopx_FREEARGS;
            }
            datbytes = bintex_ss((unsigned char*)loopdat->sval[0], (unsigned char*)loopbytes, (int)datbytes);
        }
        
        /// Copy Command data into char buffer
        {   size_t linealloc;
            linealloc   = strlen(loopcmd->sval[0]) + 2 + (bsize_val*2)+ 1;
            cmdline     = calloc(linealloc, sizeof(char));
            if (cmdline == NULL) {
                rc = -6;
                goto cmd_loopx_FREEARGS;
            }
            strcpy(cmdline, loopcmd->sval[0]);
        }
        
        /// Free argtable & argv now that they are not necessary
        cmd_loopx_FREEARGS:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        cmdutils_freeargv(argv);
    }
    
    /// Command Running & Looping
    /// 1. Acquire Command pointer from input command.  Exit if bad command.
    /// 2. Squelch the dterm output so parser doesn't dump tons of command 
    ///    response information to stdout.
    /// 3. Set up a subscriber in order to get feedback loop from the parser.
    /// 4. unlock the dtwrite mutex to allow parser thread to operate at all.
    /// 5. Loop through the data, block by block, one command for each block
    /// 6. Relock the dtwrite mutex to block the parser thread while xloop runs.
    /// 7. Destroy resources on the heap
    if (rc == 0) {
        char* cmdhead;
        char* cmdblock;
        FILE* fp_out;
        
        // Acquire Command Pointer and command strings
        cmdptr = sub_cmdacquire(&cmdhead, dth, cmdline);
        if (cmdptr == NULL) {
            rc = -7;
            goto cmd_loopx_TERM;
        }
        cmdblock = cmdhead + strlen(cmdhead);
    
        // Squelch dterm output and get file pointer for dterm-out
        fd_out = dterm_squelch(dth->dt);
        if (fd_out > 0) {
            rc = -8;
            goto cmd_loopx_TERM;
        }
        fp_out = fdopen(fd_out, "w");   //don't close this!  Merely fd --> fp conversion
        if (fp_out == NULL) {
            rc = -9;
            goto cmd_loopx_TERM;
        }
        
        // Set up subscriber, and open it
        ///@todo check command for alp.  Right now it's hard coded to FDP
        subscription = subscriber_new(dth->subscribers, 1, 0, 0);
        if (subscription == NULL) {
            rc = -10;
            goto cmd_loopx_TERM1;
        }
        if (subscriber_open(subscription, 1) != 0) {
            rc = -11;
            goto cmd_loopx_TERM2;
        }

        // Unlock the dtwrite mutex so parser thread can operate
        pthread_mutex_unlock(dth->dtwrite_mutex);
        
        // Loop the command until all data is gone
        for (dat_i=0; dat_i<datbytes; dat_i+=bsize_val) {
            int blocksz;
            blocksz = (datbytes-dat_i < bsize_val) ? datbytes-dat_i : bsize_val;
            cmdutils_uint8_to_hexstr(&cmdblock[1], &loopbytes[dat_i], blocksz);
            cmdblock[0]         = '[';
            cmdblock[1+blocksz] = ']';
            cmdblock[2+blocksz] = 0;
            
            rc = sub_cmdrun(dth, cmdptr, cmdhead, dst, dstmax);
            if (rc < 0) {
                fprintf(fp_out, "--> Command Runtime Error: (%i)\n", rc);
                break;
            }
            
            rc = subscriber_wait(subscription, timeout_val);
            if (rc == 0) {
                ///@todo check signal
                fprintf(fp_out, "\rStatus: %i/%i ", dat_i, datbytes);
                retries_cnt = retries_val;
            }
            else if (rc == ETIMEDOUT) {
                if (--retries_cnt <= 0) {
                    fprintf(fp_out, "--> Command Timeout Error\n");
                    break;
                }
                dat_i -= bsize_val;
            }
            else {
                fprintf(fp_out, "--> Internal Error: (%i)\n", rc);
            }
        }
        
        // Relock dtwrite mutex to block parser
        pthread_mutex_lock(dth->dtwrite_mutex);
        
        // Delete Subscriber (also closes), unsquelch, free
        cmd_loopx_TERM2:
        subscriber_del(dth->subscribers, subscription);
        cmd_loopx_TERM1:
        dterm_unsquelch(dth->dt);
        cmd_loopx_TERM:
        free(loopbytes);
        free(cmdline);
    }
    
    cmd_loopx_EXIT:
    return rc;
}

