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
#include <talloc.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>


typedef enum {
    DATA_binary,
    DATA_bintex,
    DATA_text,
} DATA_enum;

typedef struct {
    int bsize_val;
    int timeout_val;
    int retries_val;
    int retries_cnt;
    
    DATA_enum data_type;
    int data_size;
    uint8_t* data;
    
    char* cmdline;
} loop_input_t;



///@todo command line processing should be a function used in all cases of
/// command line processing (mainly, in dterm.c as well as here).  These three
/// subroutines should be consolidated into some helper function lib.


static size_t sub_str_mark(char* str, size_t max) {
    char* s1 = str;
    while ((*str!=0) && (*str!='\n') && (max!=0)) {
        max--;
        str++;
    }
    if (*str=='\n') *str = 0;
    
    return (str - s1);
}


///@todo integrate with cmd_lineproc() by changing loop to char* cmdline
static const cmdtab_item_t* sub_cmdproc(loop_input_t* loop, dterm_handle_t* dth) {
    /// Get each line from the pipe.
    const cmdtab_item_t* cmdptr;
    char cmdname[32];
    int cmdlen;
    char* mark;
    char* cmdhead;
    otter_app_t* appdata = dth->ext;

    cmdlen  = (int)strlen(loop->cmdline);
    cmdhead = loop->cmdline;
    
    // Convert leading and trailing quotes into spaces
    mark = strchr(cmdhead, '"');
    if (mark == NULL) {
        return NULL;
    }
    *mark = ' ';
    mark = strrchr(cmdhead, '"');
    if (mark == NULL) {
        return NULL;
    }
    *mark = ' ';
    
    // Burn whitespace ahead of command, and burn trailing whitespace
    while (isspace(cmdhead[0])) {
        cmdhead++;
        --cmdlen;
    }
    while (isspace(cmdhead[cmdlen-1])) {
        cmdhead[cmdlen-1] = 0;
        --cmdlen;
    }
    
    // Then determine length until newline, or null.
    // then search/get command in list.
    cmdlen  = (int)sub_str_mark(cmdhead, (size_t)cmdlen);
    cmdlen  = cmd_getname(cmdname, cmdhead, sizeof(cmdname));
    cmdptr  = cmd_search(appdata->cmdtab, cmdname);
    
    // Rewrite loop->cmdline to remove the wrapper parts
    strcpy(loop->cmdline, cmdhead+cmdlen);

    return cmdptr;
}


static int sub_cmdrun(dterm_handle_t* dth, const cmdtab_item_t* cmdptr, char* cmddata, uint8_t* dst, size_t dstmax) {
    /// Get each line from the pipe.
    int bytesout;
    int bytesin = 0;
    otter_app_t* appdata = dth->ext;
    
    bytesout = cmd_run(cmdptr, dth, dst, &bytesin, (uint8_t*)cmddata, dstmax);
    if (bytesout > 0) {
        if (pktlist_add_tx(&appdata->endpoint, NULL, appdata->tlist, dst, bytesout) != NULL) {
            pthread_mutex_lock(appdata->tlist_cond_mutex);
            appdata->tlist_cond_inactive = false;
            pthread_cond_signal(appdata->tlist_cond);
            pthread_mutex_unlock(appdata->tlist_cond_mutex);
        }
    }
    
    return bytesout;
}



static int sub_getinput(TALLOC_CTX* tctx, loop_input_t* loop, const char* cmdname, int* inbytes, uint8_t* src) {
    int rc = 0;
    int argc;
    char** argv;
    
    argc = cmdutils_parsestring(tctx, &argv, cmdname, (char*)src, (size_t)*inbytes);
    
    if (argc <= 0) {
        rc = -256 + argc;;
    }
    else {
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
            goto sub_getinput_FREEARGS;
        }
        
        loop->bsize_val     = (bsize->count > 0)    ? bsize->ival[0]    : 128;
        loop->timeout_val   = (timeout->count > 0)  ? timeout->ival[0]  : 1000;
        loop->retries_val   = (retries->count > 0)  ? retries->ival[0]  : 3;
        loop->retries_cnt   = loop->retries_val;
        loop->data_type     = DATA_binary;
        loop->data_size     = 0;
        
        /// Copy File Data into binary buffer
        if (file->count > 0) {
            FILE* fp;
            
            fp = fopen(loopdat->sval[0], "r");
            if (fp == NULL) {
                rc = -3;
                goto sub_getinput_CLOSEFILE;
            }
            
            // Get size of file and allocate a buffer big enough to store it.
            if (fseek(fp, 0, SEEK_END) != 0) {
                rc = -4;
                goto sub_getinput_CLOSEFILE;
            }
            loop->data_size = (int)ftell(fp);
            rewind(fp);
            
            loop->data = talloc_zero_size(tctx, (loop->data_size + 1) * sizeof(uint8_t));
            if (loop->data == NULL) {
                rc = -5;
                goto sub_getinput_CLOSEFILE;
            }
            
            // Determine file type, which is how to handle file.
            // binary: in chunks of bsize.
            // bintex: conversion to binary, then chunks of bsize.
            // text: line by line, cutoff at bsize.
            {   const char* extension;
                extension = &(loopdat->sval[0][strlen(loopdat->sval[0])-4]);
                if (strcmp(extension, ".btx") == 0) {
                    loop->data_type = DATA_bintex;
                }
                else if (strcmp(extension, ".txt") == 0) {
                    loop->data_type = DATA_text;
                }
                else if (strcmp(extension, ".hex") == 0) {
                    loop->data_type = DATA_text;
                }
                else {
                    loop->data_type = DATA_binary;
                }
            }
            
            switch (loop->data_type) {
                default:
                case DATA_binary:
                    if (fread(loop->data, loop->data_size, sizeof(uint8_t), fp) == 0) {
                        rc = -6;
                    }
                    break;
                    
                case DATA_bintex:
                    loop->data_size = bintex_fs(fp, loop->data, loop->data_size);
                    if (loop->data_size < 0) {
                        rc = -6;
                    }
                    break;
                    
                case DATA_text: {
                    char* cursor = (char*)loop->data;
                    while (1) {
                        int val = fgetc(fp);
                        if (val == EOF) {
                            break;
                        }
                        
                        *cursor = val;
                        
                        if (val == '\r') {
                            val = fgetc(fp);
                            if (val == EOF) {
                                cursor++;
                                break;
                            }
                            if (val == '\n') {
                                *cursor = '\n';
                            }
                            else {
                                cursor++;
                                *cursor = val;
                            }
                        }

                        cursor++;
                    }
                    
                    // Reset data size after  CRLF -> LF conversion
                    loop->data_size = (int)((uint8_t*)cursor - loop->data);
                    
                } break;
            }
            
            sub_getinput_CLOSEFILE:
            fclose(fp);
        }
        
        /// Copy line input into buffer for loop
        else {
            loop->data_size = (int)strlen(loopdat->sval[0]) / 2;
            loop->data      = talloc_size(tctx, loop->data_size);
            if (loop->data == NULL) {
                rc = -5;
            }
            else {
                loop->data_size = bintex_ss((unsigned char*)loopdat->sval[0], (unsigned char*)loop->data, (int)loop->data_size);
            }
        }
        
        /// Copy Command data into Command Line Buffer
        if (rc == 0) {
            size_t linealloc;
            linealloc       = strlen(loopcmd->sval[0]) + 1;
            loop->cmdline   = talloc_zero_size(tctx, linealloc * sizeof(char));
            if (loop->cmdline == NULL) {
                rc = -6;
            }
            else {
                strcpy(loop->cmdline, loopcmd->sval[0]);
            }
        }
        
        /// Free argtable & argv now that they are not necessary
        sub_getinput_FREEARGS:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        cmdutils_freeargv(tctx, argv);
    }
    
    return rc;
}


static void sub_freeinput(loop_input_t* loop) {
    if (loop != NULL) {
        talloc_free(loop->data);
        talloc_free(loop->cmdline);
    }
}


static int sub_binaryblock(char* dst, loop_input_t* loop, int dat_i) {
    int blocksz;
    blocksz         = (loop->data_size-dat_i < loop->bsize_val) ? loop->data_size-dat_i : loop->bsize_val;
    dst             = stpcpy(dst, loop->cmdline);
    dst[0]          = '[';
    cmdutils_uint8_to_hexstr(&dst[1], &loop->data[dat_i], blocksz);
    dst[1+blocksz]  = ']';
    dst[2+blocksz]  = 0;

    return blocksz;
}


static int sub_textblock(char* dst, loop_input_t* loop, int cursor) {
    int block_sz;
    int terminator_sz;
    char* newline;
    
    newline = strchr((const char*)&loop->data[cursor], '\n');
    if (newline == NULL) {
        block_sz        = loop->data_size - cursor;
        terminator_sz   = 0;
    }
    else {
        block_sz        = (int)((uint8_t*)newline - &loop->data[cursor]);
        terminator_sz   = 1;
    }
    
    if (block_sz != 0) {
        if (block_sz > loop->bsize_val) {
            block_sz        = loop->bsize_val;
            terminator_sz   = 0;
        }
        dst             = stpcpy(dst, loop->cmdline);
        dst[0]          = '"';
        memcpy(&dst[1], &loop->data[cursor], block_sz);
        dst[1+block_sz] = '"';
        dst[2+block_sz] = 0;
    }
    
    return block_sz + terminator_sz;
}



int cmd_xloop(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    static const char xloop_name[] = "xloop";
    int rc;
    int dat_i;
    int fd_out;
    const cmdtab_item_t* cmdptr;
    subscr_t subscription;
    loop_input_t loop;
    char* xloop_cmd;
    otter_app_t* appdata;
    TALLOC_CTX* xloop_heap;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();

    appdata     = dth->ext;
    xloop_heap  = talloc_new(dth->tctx);
    if (xloop_heap == NULL) {
        ///@todo better error code for out of memory
        return -1;
    }
    
    /// Stage 1: Command Line Protocol Extraction
    loop.data = NULL;
    loop.cmdline = NULL;
    rc = sub_getinput(xloop_heap, &loop, xloop_name, inbytes, src);

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
        int blocksize;

        // Process Command Line and determine loop command
        cmdptr = sub_cmdproc(&loop, dth);
        if (cmdptr == NULL) {
            rc = -7;
            goto cmd_xloop_TERM;
        }

        xloop_cmd = talloc_zero_size(xloop_heap, (strlen(loop.cmdline) + 2*loop.bsize_val + 2 + 1) * sizeof(char));
        if (xloop_cmd == NULL) {
            rc = -8;
            goto cmd_xloop_TERM;
        }

        // Squelch dterm output and get file pointer for dterm-out
        fd_out = dterm_squelch(dth);
        if (fd_out < 0) {
            rc = -9;
            goto cmd_xloop_TERM1;
        }

        // Set up subscriber, and open it
        ///@todo check command for alp.  Right now it's hard coded to FDP (1)
        subscription = subscriber_new(appdata->subscribers, 1, 0, 0);
        if (subscription == NULL) {
            rc = -11;
            goto cmd_xloop_TERM2;
        }
        if (subscriber_open(subscription, 1) != 0) {
            rc = -12;
            goto cmd_xloop_TERM3;
        }

        // Unlock the dtwrite mutex so parser thread can operate
        pthread_mutex_unlock(dth->iso_mutex);

        // Loop the command until all data is gone
        blocksize = loop.bsize_val;
        for (dat_i=0; dat_i<loop.data_size; ) {
        
            switch (loop.data_type) {
                default:
                case DATA_binary:
                case DATA_bintex:
                    blocksize = sub_binaryblock(xloop_cmd, &loop, dat_i);
                    break;
                case DATA_text:
                    blocksize = sub_textblock(xloop_cmd, &loop, dat_i);
                    break;
            }

            rc = sub_cmdrun(dth, cmdptr, xloop_cmd, dst, dstmax);
            if (rc < 0) {
                rc += -256;
                strcpy((char*)dst, "Runtime Error");
                //dterm_force_error(fd_out, "xloop", rc, 0, "Runtime Error");
                break;
            }

            rc = subscriber_wait(subscription, loop.timeout_val);
            if (rc == 0) {
                char printbuf[80];
                size_t printsz;
                
                ///@todo check signal
                
                dat_i += blocksize;
                
                printsz = sprintf(printbuf, "Status %i/%i (%i%%)", dat_i, loop.data_size, (int)((100*dat_i)/loop.data_size));
                
                if (dth->intf->type == INTF_interactive) {
                    write(fd_out, VT100_CLEAR_LN, sizeof(VT100_CLEAR_LN));
                    write(fd_out, printbuf, printsz);
                }
                else {
                    dterm_force_cmdmsg(fd_out, "xloop", printbuf);
                }
                
                loop.retries_cnt = loop.retries_val;
            }
            else if (rc == ETIMEDOUT) {
                if (--loop.retries_cnt <= 0) {
                    strcpy((char*)dst, "Command Timeout Error");
                    //dterm_force_error(fd_out, "xloop", -13, 0, "Command Timeout Error");
                    rc = -13;
                    break;
                }
            }
            else {
                strcpy((char*)dst, "Subscriber Error");
                //dterm_force_error(fd_out, "xloop", rc, 0, "Internal Error");
                rc = -14;
                break;
            }
        }
        if (dth->intf->type == INTF_interactive) {
            write(fd_out, "\n", 1);
        }

        // Relock dtwrite mutex to block parser
        pthread_mutex_lock(dth->iso_mutex);

        // Delete Subscriber (also closes), unsquelch, free
        cmd_xloop_TERM3:
        subscriber_del(appdata->subscribers, subscription);
        cmd_xloop_TERM2:
        dterm_unsquelch(dth);
        cmd_xloop_TERM1:
        //talloc_free(xloop_cmd);
        cmd_xloop_TERM:
        //sub_freeinput(&loop);
        talloc_free(xloop_heap);
    }

    cmd_xloop_EXIT:
    return rc;
}



int cmd_sendhex(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    static const char sendhex_name[] = "sendhex";
    static const char xloop_fmt[] = "-f \"file wo 255 [5A5A]\" %s";
    int rc;
    int argc;
    char** argv;
    char* xloop_cmd = NULL;
    int xloop_bytes;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(dth->tctx, &argv, sendhex_name, (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
    }
    else {
        struct arg_file* hexfile= arg_file1(NULL,NULL,"<.hex file>", "Motorola .hex File to send to target.");
        struct arg_end* end     = arg_end(2);
        void* argtable[]        = { hexfile, end };
        
        rc = cmdutils_argcheck(argtable, end, argc, argv);
        if (rc != 0) {
            rc = -1;
            goto cmd_sendhex_EXEC;
        }
        
        if (strcmp(hexfile->extension[0], ".hex") != 0) {
            strcpy((char*)dst, "Input Error, file not with .hex");
            rc = -2;
            goto cmd_sendhex_EXEC;
        }
        
        xloop_cmd = talloc_zero_size(dth->tctx, (strlen(xloop_fmt) + strlen(hexfile->filename[0]) + 1) * sizeof(char));
        if (xloop_cmd == NULL) {
            rc = -1;
            goto cmd_sendhex_EXEC;
        }
        
        xloop_bytes = sprintf(xloop_cmd, xloop_fmt, hexfile->filename[0]);
        
        cmd_sendhex_EXEC:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        cmdutils_freeargv(dth->tctx, argv);
        if (rc == 0) {
            //fprintf(stderr, "TO XLOOP: <<%s>>\n", xloop_cmd);
            rc = cmd_xloop(dth, dst, &xloop_bytes, (uint8_t*)xloop_cmd, dstmax);
        }
        talloc_free(xloop_cmd);

    }
    
    //fprintf(stderr, "sendhex err=%i\n", rc);
    
    return rc;
}


