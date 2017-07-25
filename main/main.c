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
/**
  * @file       main.c
  * @author     JP Norair & Oleksandr Pereverzyev
  * @version    R100
  * @date       25 July 2014
  * @brief      Otter main() function and global data declarations
  * @defgroup   Otter
  * @ingroup    Otter
  * 
  * Otter (OpenTag TERminal) is a threaded, POSIX-C app that provides a console
  * and shell-like interface between a normal TTY and the OpenTag MPipe.  MPipe
  * is a binary, packet-oriented serial interface often implemented over 
  * TTY/RS232 or USB-CDC-ACM (aka virtual TTY).  Additionally, Otter can do
  * deep inspection and translation of M2DEF binary payloads commonly used with
  * MPipe and OpenTag.
  *
  * See http://wiki.indigresso.com for more information and documentation.
  * 
  ******************************************************************************
  */

// Application Headers
#include "dterm.h"
#include "mpipe.h"
#include "ppipe.h"
#include "ppipelist.h"
#include "cmdsearch.h"
#include "cmdhistory.h"

// Local Libraries
#include "argtable3.h"
#include "cJSON.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>




//Comment-out if not testing: this should ideally be passed into the compiler
//params, but with XCode that's a mystery.
//#define __TEST__

#define _OTTER_VERSION      "0.2.0"
#define _OTTER_DATE         "6.2017"
#define _DEFAULT_BAUDRATE   115200






// Client Data type
///@todo some of this should get merged into MPipe data type

typedef enum {
    FORMAT_Dynamic  = 0,
    FORMAT_Bintex   = 1,
    FORMAT_Hex8     = 2,
    FORMAT_Hex16    = 3,
    FORMAT_Hex32    = 4
} FORMAT_Type;

typedef struct {
    bool        verbose_on;
    FORMAT_Type format;
} cliopt_struct;

typedef struct {
    //FILE*           out;
    //FILE*           in;
    //cliopt_struct   opt;
    //bool            block_in;
    
    int             exitcode;
    pthread_mutex_t kill_mutex;
    pthread_cond_t  kill_cond;
    
    char*           call_path;
    
} cli_struct;

cli_struct cli;
















/** main functions & signal handlers <BR>
  * ========================================================================<BR>
  * 
  */

void _assign_signal(int sigcode, void (*sighandler)(int)) {
    if (signal(sigcode, sighandler) != 0) {
        fprintf(stderr, "--> Error assigning signal (%d), exiting\n", sigcode);
        exit(EXIT_FAILURE);
    }
}

void sigint_handler(int sigcode) {
    cli.exitcode = EXIT_FAILURE;
    pthread_cond_signal(&cli.kill_cond);
}

void sigquit_handler(int sigcode) {
    cli.exitcode = EXIT_SUCCESS;
    pthread_cond_signal(&cli.kill_cond);
}


int otter_main( const char* ttyfile,
                int baudrate,
                bool verbose,
                cJSON* params
                ); 


void sub_json_loadargs(cJSON* json, 
                       char* ttyfile, 
                       int* baudrate_val, 
                       bool* verbose_val );

void ppipelist_populate(cJSON* obj);


// NOTE Change to transparent mutex usage or not?
/*
void _cli_block(bool set) {
    pthread_mutex_lock(&cli.block_mutex);
    cli.block_in = set;
    pthread_mutex_unlock(&cli.block_mutex);
}

bool _cli_is_blocked(void) {
    volatile bool retval;
    pthread_mutex_lock(&cli.block_mutex);
    retval = cli.block_in;
    pthread_mutex_unlock(&cli.block_mutex);
    
    return retval;
}
*/







// notes:
// 0. ch_inc/ch_dec (w/ ?:) and history pointers can be replaced by integers and modulo
// 1. command history is glitching when command takes whole history buf (not c string)
// 2. delete and remove line do not work for multiline command

static dterm_t* _dtputs_dterm;

int _dtputs(char* str) {
    return dterm_puts(_dtputs_dterm, str);
}








int main(int argc, const char * argv[]) {
/// ArgTable params: These define the input argument behavior
    struct arg_file *ttyfile = arg_file1(NULL,NULL,"<ttyfile>",         "path to tty file (e.g. /dev/tty.usbmodem)");
    struct arg_int  *brate   = arg_int0(NULL,NULL,"<baudrate>",         "baudrate, default is 115200");
    //struct arg_str  *parsers = arg_str1("p", "parsers", "<msg:parser>", "parser call string with comma-separated msg:parser pairs");
    //struct arg_str  *fparse  = arg_str1("P", "parsefile", "<file>",     "file containing comma-separated msg:parser pairs");
    
    // Generic
    struct arg_file *config  = arg_file0("C", "config", "<file.json>",   "JSON based configuration file.");
    struct arg_lit  *verbose = arg_lit0("v","verbose",                  "use verbose mode");
    struct arg_lit  *help    = arg_lit0(NULL,"help",                    "print this help and exit");
    struct arg_lit  *version = arg_lit0(NULL,"version",                 "print version information and exit");
    struct arg_end  *end     = arg_end(20);
    
    void* argtable[] = {ttyfile,brate,config,verbose,help,version,end};
    const char* progname = "otter";
    int nerrors;
    int exitcode = 0;
    
    char ttyfile_val[256];
    int  baudrate_val = _DEFAULT_BAUDRATE;
    bool verbose_val  = false;
    
    cJSON* json = NULL;
    char* buffer = NULL;

    if (arg_nullcheck(argtable) != 0) {
        /// NULL entries were detected, some allocations must have failed 
        fprintf(stderr, "%s: insufficient memory\n", progname);
        exitcode=1;
        goto main_EXIT;
    }

    /* Parse the command line as defined by argtable[] */
    nerrors = arg_parse(argc, argv, argtable);

    /// special case: '--help' takes precedence over error reporting
    if (help->count > 0) {
        printf("Usage: %s", progname);
        arg_print_syntax(stdout, argtable, "\n");
        
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        
        exitcode = 0;
        goto main_EXIT;
    }

    /// special case: '--version' takes precedence error reporting 
    if (version->count > 0) {
        printf("%s -- %s\n", _OTTER_VERSION, _OTTER_DATE);
        printf("Designed by JP Norair, Haystack Technologies, Inc.\n");
        
        exitcode = 0;
        goto main_EXIT;
    }

    /// If the parser returned any errors then display them and exit
    /// - Display the error details contained in the arg_end struct.
    if (nerrors > 0) {
        arg_print_errors(stdout,end,progname);
        printf("Try '%s --help' for more information.\n", progname);
        
        exitcode = 1;
        goto main_EXIT;
    }

    /// special case: uname with no command line options induces brief help 
    if (argc==1) {
        printf("Try '%s --help' for more information.\n",progname);
        
        exitcode = 0;
        goto main_EXIT;
    }

    /// Do some final checking of input values
    ///@todo Validate that we're looking at something like "/dev/tty.usb..."
    
        /// Get JSON config input.  Priority is direct input vs. file input
    /// 1. There is an "arguments" object that works the same as the command 
    ///    line arguments.  
    /// 2. Any other objects may be used in custom ways by the app itself.
    ///    In particular, they can be used for loading custom keys & certs.
    if (config->count > 0) {
        FILE* fp;
        long lSize;
        fp = fopen(config->filename[0], "r");
        if (fp == NULL) {
            exitcode = (int)'f';
            goto main_EXIT;
        }

        fseek(fp, 0L, SEEK_END);
        lSize = ftell(fp);
        rewind(fp);

        buffer = calloc(1, lSize+1);
        if (buffer == NULL) {
            exitcode = (int)'m';
            goto main_EXIT;
        }

        if(fread(buffer, lSize, 1, fp) == 1) {
            json = cJSON_Parse(buffer);
            fclose(fp);
        }
        else {
            fclose(fp);
            fprintf(stderr, "read to %s fails\n", config->filename[0]);
            exitcode = (int)'r';
            goto main_EXIT;
        }

        /// At this point the file is closed and the json is parsed into the
        /// "json" variable.  
        if (json == NULL) {
            fprintf(stderr, "JSON parsing failed.  Exiting.\n");
            goto main_EXIT;
        }
        
        sub_json_loadargs(json, ttyfile_val, &baudrate_val, &verbose_val);
    }
    
    /// If no JSON file, then configuration should be through the arguments.
    /// If both exist, then the arguments will override JSON.
    /// Handle case with explicit host:port.  Explicit host/port will override
    /// a supplied url.
    if (ttyfile->count == 0) {
        printf("Input error: no tty provided\n");
        printf("Try '%s --help' for more information.\n", progname);
        goto main_EXIT;
    }
    strncpy(ttyfile_val, ttyfile->filename[0], 256);
    
    if (brate->count != 0) {
        baudrate_val = brate->ival[0];
    }
    if (verbose->count != 0) {
        verbose_val = true;
    }
    

    exitcode = otter_main(  (const char*)ttyfile_val, 
                            baudrate_val, 
                            verbose_val,
                            json);
                            
    main_EXIT:
    if (json != NULL) {
        cJSON_Delete(json);
    }
    if (buffer != NULL) {
        free(buffer);
    }
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));

    return exitcode;
}




/// What this should do is start two threads, one for the character I/O on
/// the dterm side, and one for the serial I/O.
int otter_main(const char* ttyfile, int baudrate, bool verbose, cJSON* params) {    
    // MPipe Datastructs
    mpipe_arg_t mpipe_args;
    mpipe_ctl_t mpipe_ctl;
    pktlist_t   mpipe_tlist;
    pktlist_t   mpipe_rlist;
    
    // DTerm Datastructs
    dterm_arg_t dterm_args;
    dterm_t     dterm;
    cmdhist     cmd_history;
    
    // Child Threads (4)
    pthread_t   thr_mpreader;
    pthread_t   thr_mpwriter;
    pthread_t   thr_mpparser;
    pthread_t   thr_dtprompter;
    
    // Mutexes used with Child Threads
    pthread_mutex_t dtwrite_mutex;          // For control of writeout to dterm
    pthread_mutex_t rlist_mutex;
    pthread_mutex_t tlist_mutex;
    
    // Cond/Mutex used with Child Threads
    pthread_cond_t  tlist_cond;
    pthread_mutex_t tlist_cond_mutex;
    pthread_cond_t  pktrx_cond;
    pthread_mutex_t pktrx_mutex;
    
    
    /// JSON params construct should contain the following objects
    /// - "msgcall": { "msgname1":"call string 1", "msgname2":"call string 2" }
    /// - TODO "msgpipe": (same as msg call, but call is open at startup and piped-to)
    //mpipe_args.msgcall = cJSON_GetObjectItem(params, "msgcall");
    

    /// Initialize command search table.  
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.
    cmdsearch_init(NULL);
    
    
    /// Initialize packet lists for transmitted packets and received packets
    pktlist_init(&mpipe_rlist);
    pktlist_init(&mpipe_tlist);
    
    
    /// Initialize the ppipe system of named pipes, based on the input json
    /// configuration file.  We have input and output pipes of several types.
    /// @todo determine what all these types are.  They must work with the 
    ///       relatively simple Gateway API.
    /// @todo the "basepath" input to ppipelist_init() can be an argument 
    ///       or some other configuration element.
    ppipelist_init("./");
    if (params != NULL) {
        cJSON* obj;
        
        for (obj=params->child; obj!=NULL; obj=obj->next) {
            if (strcmp(obj->string, "pipes") != 0) {
                continue;
            }
            
            /// This is the pipes object, the only one we care about here
            for (obj=obj->child; obj!=NULL; obj=obj->next) {
                ppipelist_populate(obj);
            }
            break;
        }
    }
    
    /// Initialize Thread Mutexes & Conds.  This is finnicky and it must be
    /// done before assignment into the argument containers, possibly due to 
    /// C-compiler foolishly optimizing.
    assert( pthread_mutex_init(&dtwrite_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&rlist_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&tlist_mutex, NULL) == 0 );
    
    assert( pthread_mutex_init(&cli.kill_mutex, NULL) == 0 );
    pthread_cond_init(&cli.kill_cond, NULL);
    assert( pthread_mutex_init(&tlist_cond_mutex, NULL) == 0 );
    pthread_cond_init(&tlist_cond, NULL);
    assert( pthread_mutex_init(&pktrx_mutex, NULL) == 0 );
    pthread_cond_init(&pktrx_cond, NULL);

    
    /// Open the mpipe TTY & Setup MPipe threads
    /// The MPipe Filename (e.g. /dev/ttyACMx) is sent as the first argument
    mpipe_args.mpctl            = &mpipe_ctl;
    mpipe_args.rlist            = &mpipe_rlist;
    mpipe_args.tlist            = &mpipe_tlist;
    mpipe_args.puts_fn          = &_dtputs;
    mpipe_args.dtwrite_mutex    = &dtwrite_mutex;
    mpipe_args.rlist_mutex      = &rlist_mutex;
    mpipe_args.tlist_mutex      = &tlist_mutex;
    mpipe_args.tlist_cond_mutex = &tlist_cond_mutex;
    mpipe_args.tlist_cond       = &tlist_cond;
    mpipe_args.pktrx_mutex      = &pktrx_mutex;
    mpipe_args.pktrx_cond       = &pktrx_cond;
    mpipe_args.kill_mutex       = &cli.kill_mutex;
    mpipe_args.kill_cond        = &cli.kill_cond;
    if (mpipe_open(&mpipe_ctl, ttyfile, baudrate, 8, 'N', 1, 0, 0, 0) < 0) {
        cli.exitcode = -1;
        goto otter_main_TERM1;
    }
    
    /// Open DTerm interface & Setup DTerm threads
    /// The dterm thread will deal with all other aspects, such as command
    /// entry and history initialization.
    ///@todo "STDIN_FILENO" and "STDOUT_FILENO" could be made dynamic
    _dtputs_dterm               = &dterm;
    dterm.fd_in                 = STDIN_FILENO;
    dterm.fd_out                = STDOUT_FILENO;
    dterm_args.ch               = ch_init(&cmd_history);
    dterm_args.dt               = &dterm;
    dterm_args.tlist            = &mpipe_tlist;
    dterm_args.dtwrite_mutex    = &dtwrite_mutex;
    dterm_args.tlist_mutex      = &tlist_mutex;
    dterm_args.tlist_cond       = &tlist_cond;
    dterm_args.kill_mutex       = &cli.kill_mutex;
    dterm_args.kill_cond        = &cli.kill_cond;
    if (dterm_open(&dterm) < 0) {
        cli.exitcode = -2;
        goto otter_main_TERM2;
    }

    
    /// Initialize the signal handlers for this process.
    /// These are activated by Ctl+C (SIGINT) and Ctl+\ (SIGQUIT) as is
    /// typical in POSIX apps.  When activated, the threads are halted and
    /// Otter is shutdown.
    cli.exitcode = EXIT_SUCCESS;
    _assign_signal(SIGINT, &sigint_handler);
    _assign_signal(SIGQUIT, &sigquit_handler);

    
    /// Invoke the child threads below.  All of the child threads run
    /// indefinitely until an error occurs or until the user quits.  Quit can 
    /// be via Ctl+C or Ctl+\, or potentially also through a dterm command.  
    /// Each thread must be be implemented to raise SIGQUIT or SIGINT on exit
    /// i.e. raise(SIGQUIT).
    pthread_create(&thr_mpreader, NULL, &mpipe_reader, (void*)&mpipe_args);
    pthread_create(&thr_mpwriter, NULL, &mpipe_writer, (void*)&mpipe_args);
    pthread_create(&thr_mpparser, NULL, &mpipe_parser, (void*)&mpipe_args);
    pthread_create(&thr_dtprompter, NULL, &dterm_prompter, (void*)&dterm_args);

    
    /// Threads are now running.  The rest of the main() code, below, is
    /// blocked by pthread_cond_wait() until the kill_cond is sent by one of 
    /// the child threads.  This will cause the program to quit.
    pthread_mutex_lock(&cli.kill_mutex);
    pthread_cond_wait(&cli.kill_cond, &cli.kill_mutex);
    pthread_cancel(thr_mpreader);
    pthread_cancel(thr_mpwriter);
    pthread_cancel(thr_mpparser);
    pthread_cancel(thr_dtprompter);
    
    otter_main_TERM:
    pthread_mutex_unlock(&dtwrite_mutex);
    pthread_mutex_destroy(&dtwrite_mutex);
    pthread_mutex_unlock(&rlist_mutex);
    pthread_mutex_destroy(&rlist_mutex);
    pthread_mutex_unlock(&tlist_mutex);
    pthread_mutex_destroy(&tlist_mutex);
    
    pthread_mutex_unlock(&tlist_cond_mutex);
    pthread_mutex_destroy(&tlist_cond_mutex);
    pthread_cond_destroy(&tlist_cond);
    
    pthread_mutex_unlock(&pktrx_mutex);
    pthread_mutex_destroy(&pktrx_mutex);
    pthread_cond_destroy(&pktrx_cond);
    
    pthread_mutex_unlock(&cli.kill_mutex);
    pthread_mutex_destroy(&cli.kill_mutex);
    pthread_cond_destroy(&cli.kill_cond);
    
    
    /// Close the drivers/files and free all allocated data objects (primarily 
    /// in mpipe).
    dterm_close(&dterm);
    
    otter_main_TERM2:
    mpipe_close(&mpipe_ctl);
    dterm_free(&dterm);
    ch_free(&cmd_history);
    
    otter_main_TERM1:
    mpipe_freelists(&mpipe_rlist, &mpipe_tlist);
    ppipelist_deinit();
    
    // cli.exitcode is set to 0, unless sigint is raised.
#   ifdef __TEST__
    fprintf(stderr, "Exiting Cleanly\n"); 
#   endif

    return cli.exitcode;
}



void sub_json_loadargs(cJSON* json, 
                       char* ttyfile, 
                       int* baudrate_val, 
                       bool* verbose_val ) {
                       
#   define GET_STRING_ARG(DST, LIMIT, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                strncpy(DST, arg->valuestring, LIMIT);   \
            }   \
        }   \
    } while(0)
    
#   define GET_INT_ARG(DST, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsNumber(arg) != 0) {    \
                *DST = (int)arg->valueint;   \
            }   \
        }   \
    } while(0)
    
#   define GET_BOOL_ARG(DST, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsNumber(arg) != 0) {    \
                *DST = (arg->valueint != 0);   \
            }   \
        }   \
    } while(0)
    
    cJSON* arg;
    
    ///1. Get "arguments" object, if it exists
    json = cJSON_GetObjectItem(json, "arguments");
    if (json == NULL) {
        return;
    }
    
    /// 2. Systematically get all of the individual arguments
    GET_STRING_ARG(ttyfile, 256, "tty");

    GET_INT_ARG(baudrate_val, "baudrate");

    GET_BOOL_ARG(verbose_val, "verbose");
}






void ppipelist_populate(cJSON* obj) {

    if (obj != NULL) {
        const char* prefix;
        prefix = obj->string;
        
        obj = obj->child;
        while (obj != NULL) { 
            if (cJSON_IsString(obj) != 0) { 
                //printf("%s, %s, %s\n", prefix, obj->string, obj->valuestring);
                ppipelist_new(prefix, obj->string, obj->valuestring); 
                //printf("%d\n", __LINE__);
            }
            obj = obj->next;
        }
    }
}


 
