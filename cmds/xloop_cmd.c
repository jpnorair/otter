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


static const cmdtab_item_t* sub_cmdacquire(char** cmddata, dterm_handle_t* dth, char* cmdline) {
    /// Get each line from the pipe.
    const cmdtab_item_t* cmdptr;
    char cmdname[32];
    int cmdlen;

    cmdlen = (int)strlen(cmdline);
    
    // Burn whitespace ahead of command
    // Burn trailing whitespace
    while (isspace(*cmdline)) {
        cmdline++;
        --cmdlen;
    }
    while (isspace(cmdline[cmdlen-1])) {
        --cmdlen;
    }

    // Then determine length until newline, or null.
    // then search/get command in list.
    cmdlen  = (int)sub_str_mark(cmdline, (size_t)cmdlen);
    cmdlen  = cmd_getname(cmdname, cmdline, sizeof(cmdname));
    cmdptr  = cmd_search(dth->cmdtab, cmdname);
    
    *cmddata = cmdline+cmdlen;
    return cmdptr;
}

static int sub_cmdrun(dterm_handle_t* dth, const cmdtab_item_t* cmdptr, char* cmddata, uint8_t* dst, size_t dstmax) {
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


//dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax


static int sub_getinput(loop_input_t* loop, const char* cmdname, int* inbytes, uint8_t* src) {
    int rc = 0;
    int argc;
    char** argv;
    
    argc = cmdutils_parsestring(&argv, cmdname, (char*)src, (char*)src, (size_t)*inbytes);
    
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
            
            loop->data = calloc(loop->data_size + 1, sizeof(uint8_t));
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
            loop->data      = malloc(loop->data_size);
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
            linealloc       = strlen(loopcmd->sval[0]) + 2 + (loop->bsize_val*2)+ 1;
            loop->cmdline   = calloc(linealloc, sizeof(char));
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
        cmdutils_freeargv(argv);
    }
    
    return rc;
}


static void sub_freeinput(loop_input_t* loop) {
    if (loop != NULL) {
        if (loop->data != NULL) {
            free(loop->data);
        }
        if (loop->cmdline != NULL) {
            free(loop->cmdline);
        }
    }
}


static int sub_binaryblock(loop_input_t* loop, char* cmdblock, int dat_i) {
    int blocksz;
    blocksz = (loop->data_size-dat_i < loop->bsize_val) ? loop->data_size-dat_i : loop->bsize_val;
    cmdutils_uint8_to_hexstr(&cmdblock[1], &loop->data[dat_i], blocksz);
    cmdblock[0]         = '[';
    cmdblock[1+blocksz] = ']';
    cmdblock[2+blocksz] = 0;
    return blocksz;
}


static int sub_textblock(loop_input_t* loop, char* cmdblock, int cursor) {
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
        memcpy(&cmdblock[1], &loop->data[cursor], block_sz);
        cmdblock[0]             = '"';
        cmdblock[1+block_sz]    = '"';
        cmdblock[2+block_sz]    = 0;
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
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    /// Stage 1: Command Line Protocol Extraction
    loop.data = NULL;
    loop.cmdline = NULL;
    rc = sub_getinput(&loop, xloop_name, inbytes, src);

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
        int blocksize;
        
        // Acquire Command Pointer and command strings
        cmdptr = sub_cmdacquire(&cmdhead, dth, loop.cmdline);
        if (cmdptr == NULL) {
            rc = -7;
            goto cmd_xloop_TERM;
        }
        cmdblock = cmdhead + strlen(cmdhead);
    
        // Squelch dterm output and get file pointer for dterm-out
        fd_out = dterm_squelch(dth->dt);
        if (fd_out < 0) {
            rc = -8;
            goto cmd_xloop_TERM;
        }
        fp_out = fdopen(fd_out, "w");   //don't close this!  Merely fd --> fp conversion
        if (fp_out == NULL) {
            rc = -9;
            goto cmd_xloop_TERM;
        }

        // Set up subscriber, and open it
        ///@todo check command for alp.  Right now it's hard coded to FDP (1)
        subscription = subscriber_new(dth->subscribers, 1, 0, 0);
        if (subscription == NULL) {
            rc = -10;
            goto cmd_xloop_TERM1;
        }

        if (subscriber_open(subscription, 1) != 0) {
            rc = -11;
            goto cmd_xloop_TERM2;
        }

        // Unlock the dtwrite mutex so parser thread can operate
        pthread_mutex_unlock(dth->dtwrite_mutex);

        // Loop the command until all data is gone
        blocksize = loop.bsize_val;
        for (dat_i=0; dat_i<loop.data_size; dat_i+=blocksize) {
            switch (loop.data_type) {
                default:
                case DATA_binary:
                case DATA_bintex:
                    blocksize = sub_binaryblock(&loop, cmdblock, dat_i);
                    break;
                case DATA_text:
                    blocksize = sub_textblock(&loop, cmdblock, dat_i);
                    break;
            }

            rc = sub_cmdrun(dth, cmdptr, cmdhead, dst, dstmax);
            if (rc < 0) {
                rc += -256;
                fprintf(fp_out, "--> Command Runtime Error: (%i)\n", rc);
                break;
            }
fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
            rc = subscriber_wait(subscription, loop.timeout_val);
            if (rc == 0) {
                ///@todo check signal
                fprintf(fp_out, "\rStatus: %i/%i ", dat_i, loop.data_size);
                loop.retries_cnt = loop.retries_val;
            }
            else if (rc == ETIMEDOUT) {
                if (--loop.retries_cnt <= 0) {
                    fprintf(fp_out, "--> Command Timeout Error\n");
                    break;
                }
                dat_i -= loop.bsize_val;
            }
            else {
                fprintf(fp_out, "--> Internal Error: (%i)\n", rc);
            }
        }
fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        // Relock dtwrite mutex to block parser
        pthread_mutex_lock(dth->dtwrite_mutex);
fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        // Delete Subscriber (also closes), unsquelch, free
        cmd_xloop_TERM2:
        subscriber_del(dth->subscribers, subscription); fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        cmd_xloop_TERM1:
        dterm_unsquelch(dth->dt); fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        cmd_xloop_TERM:
        sub_freeinput(&loop); fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    }
    
    cmd_xloop_EXIT:
    return rc;
}



int cmd_sendhex(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    static const char sendhex_name[] = "sendhex";
    static const char xloop_fmt[] = "-f \"file wo 255 [A5A5]\" %s";
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
    
    argc = cmdutils_parsestring(&argv, sendhex_name, (char*)src, (char*)src, (size_t)*inbytes);
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
            dterm_printf(dth->dt, "--> Input Error, file not with .hex\n");
            rc = -2;
            goto cmd_sendhex_EXEC;
        }
        
        xloop_cmd = calloc(strlen(xloop_fmt) + strlen(hexfile->filename[0]) + 1, sizeof(char));
        if (xloop_cmd == NULL) {
            rc = -1;
            goto cmd_sendhex_EXEC;
        }
        
        xloop_bytes = sprintf(xloop_cmd, xloop_fmt, hexfile->filename[0]);
        
        cmd_sendhex_EXEC:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        cmdutils_freeargv(argv);
        if (rc == 0) {
            //fprintf(stderr, "TO XLOOP: <<%s>>\n", xloop_cmd);
            rc = cmd_xloop(dth, dst, &xloop_bytes, (uint8_t*)xloop_cmd, dstmax);
        }
        free(xloop_cmd);

    }
    
    fprintf(stderr, "sendhex err=%i\n", rc);
    
    return rc;
}


