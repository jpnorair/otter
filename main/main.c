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

// Top Level Configuration Header
#include "otter_cfg.h"

// Application Headers
#include "dterm.h"
#include "mpipe.h"
#include "modbus.h"
#include "ppipe.h"
#include "ppipelist.h"
#include "cmdsearch.h"
#include "cmdhistory.h"
#include "cliopt.h"
#include "user.h"
#include "debug.h"

// Local Libraries
#include "argtable3.h"
#include "cJSON.h"

//Package libraries
#include <cmdtab.h>

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



// Client Data type
///@todo some of this should get merged into MPipe data type

static cliopt_t cliopts;

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
                int enc_bits, int enc_parity, int enc_stopbits,
                bool pipe,
                const char* xpath,
                cJSON* params
                ); 

INTF_Type sub_intf_cmp(const char* s1);

void sub_json_loadargs(cJSON* json, 
                       char* ttyfile, 
                       int* baudrate_val, 
                       bool* pipe_val,
                       int* intf_val,
                       char* xpath,
                       int* enc_bits,
                       int* enc_parity,
                       int* enc_stop,
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





INTF_Type sub_intf_cmp(const char* s1) {
    INTF_Type selected_intf;

    if (strcmp(s1, "modbus") == 0) {
        selected_intf = INTF_modbus;
    }
    else if (0) {
        ///@todo add future interfaces here
    }
    else {
        selected_intf = INTF_mpipe;
    }
    
    return selected_intf;
}






int main(int argc, char* argv[]) {
/// ArgTable params: These define the input argument behavior
    struct arg_file *ttyfile = arg_file1(NULL,NULL,"<ttyfile>",         "Path to tty file (e.g. /dev/tty.usbmodem)");
    struct arg_int  *brate   = arg_int0(NULL,NULL,"<baudrate>",         "Baudrate, default is 115200");
    struct arg_str  *ttyenc  = arg_str0("e", "encoding", "<e.g. 8N1>",  "Manual-entry for TTY encoding (default mpipe:8N1, modbus:8N2)");
    struct arg_lit  *pipe    = arg_lit0("p","pipe",                     "Use pipe I/O instead of terminal console");
    struct arg_str  *intf    = arg_str0("i", "intf", "<mpipe|modbus>",  "Select \"mpipe\" or \"modbus\" interface (default=mpipe)");
    struct arg_file *xpath   = arg_file0("x", "xpath", "<filepath>",     "Path to directory of external data processor programs");
    //struct arg_str  *parsers = arg_str1("p", "parsers", "<msg:parser>", "parser call string with comma-separated msg:parser pairs");
    //struct arg_str  *fparse  = arg_str1("P", "parsefile", "<file>",     "file containing comma-separated msg:parser pairs");
    
    // Generic
    struct arg_file *config  = arg_file0("C", "config", "<file.json>",  "JSON based configuration file.");
    struct arg_lit  *verbose = arg_lit0("v","verbose",                  "Use verbose mode");
    struct arg_lit  *debug   = arg_lit0("d","debug",                    "Set debug mode on: requires compiling for debug");
    struct arg_lit  *help    = arg_lit0(NULL,"help",                    "Print this help and exit");
    struct arg_lit  *version = arg_lit0(NULL,"version",                 "Print version information and exit");
    struct arg_end  *end     = arg_end(20);
    
    void* argtable[] = {ttyfile,brate,pipe,ttyenc,intf,xpath,config,verbose,debug,help,version,end};
    const char* progname = OTTER_PARAM(NAME);
    int nerrors;
    int exitcode = 0;
    
    char ttyfile_val[256];
    int  baudrate_val   = OTTER_PARAM_DEFBAUDRATE;
    bool verbose_val    = false;
    bool pipe_val       = false;
    
    INTF_Type intf_val  = OTTER_FEATURE(MPIPE) ? INTF_mpipe : INTF_modbus;
    int enc_bits        = 8;
    int enc_parity      = (int)'N';
    int enc_stopbits    = (intf_val == INTF_modbus) ? 2 : 1;
    
    char xpath_val[256] = "";
    
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
        printf("%s -- %s\n", OTTER_PARAM_VERSION, OTTER_PARAM_DATE);
        printf("Designed by %s\n", OTTER_PARAM_BYLINE);
        printf("Based on otter by JP Norair (indigresso.com)\n");
        
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

    /// special case: with no command line options induces brief help 
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
        
        {   int tmp_intf;
            sub_json_loadargs(  json, 
                                ttyfile_val, 
                                &baudrate_val, 
                                &pipe_val, 
                                &tmp_intf, 
                                xpath_val,
                                &enc_bits, 
                                &enc_parity, 
                                &enc_stopbits, 
                                &verbose_val
                            );
            intf_val = tmp_intf;
        }
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
    if (ttyenc->count != 0) {
        enc_bits        = (int)ttyenc->sval[0][0];
        enc_parity      = (int)ttyenc->sval[0][1];
        enc_stopbits    = (int)ttyenc->sval[0][2];
    }
    if (pipe->count != 0) {
        pipe_val = true;
        verbose_val = false;
    }
    if (intf->count != 0) {
        intf_val = sub_intf_cmp(intf->sval[0]);
    }
    if (xpath->count != 0) {
        strncpy(xpath_val, xpath->filename[0], 256);
    }
    if (verbose->count != 0) {
        verbose_val = true;
    }
    
    /// Client Options.  These are read-only from internal modules
    cliopts.format      = FORMAT_Dynamic;
    cliopts.intf        = intf_val;
    if (debug->count != 0) {
        cliopts.debug_on    = true;
        cliopts.verbose_on  = true;
    }
    else {
        cliopts.debug_on    = false;
        cliopts.verbose_on  = verbose_val;
    }
    cliopt_init(&cliopts);
    
    /// All configuration is done.
    /// Send all configuration data to program main function.
    exitcode = otter_main(  (const char*)ttyfile_val, 
                            baudrate_val, 
                            enc_bits, enc_parity, enc_stopbits,
                            pipe_val,
                            (const char*)xpath_val,
                            json);
    
    ///@todo some optimization could be realized by putting this ahead of the 
    ///      call to otter_main, although the JSON part must be kept and freed
    ///      only after main runs.
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
int otter_main( const char* ttyfile, 
                int baudrate, 
                int enc_bits, int enc_parity, int enc_stopbits,
                bool pipe, 
                const char* xpath,
                cJSON* params) {    
    
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
    void*       (*dterm_fn)(void* args);
    pthread_t   thr_dterm;
    
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
    
    /// Initialize Otter Environment Variables.
    /// This must be the first module to be initialized.
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.
    
    /// Initialize the user manager.
    /// This must be done prior to command module init
    user_init();
    
    /// Initialize command search table.  
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.
    cmd_init(NULL, xpath);
    
    /// Initialize packet lists for transmitted packets and received packets
    pktlist_init(&mpipe_rlist);
    pktlist_init(&mpipe_tlist);
    
    
    /// Initialize the ppipe system of named pipes, based on the input json
    /// configuration file.  We have input and output pipes of several types.
    /// @todo determine what all these types are.  They must work with the 
    ///       relatively simple Gateway API.
    /// @todo the "basepath" input to ppipelist_init() can be an argument 
    ///       or some other configuration element.
    ppipelist_init("./pipes/");
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
    
    if (mpipe_open(&mpipe_ctl, ttyfile, baudrate, enc_bits, enc_parity, enc_stopbits, 0, 0, 0) < 0) {
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
    dterm_fn                    = (pipe == false) ? &dterm_prompter : &dterm_piper;
    
    if (dterm_open(&dterm, pipe) < 0) {
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
    /// i.e. raise(SIGINT).
    DEBUG_PRINTF("Creating theads\n");
    if (OTTER_FEATURE(MODBUS) && (cliopt_getintf() == INTF_modbus)) {
        DEBUG_PRINTF("Opening Modbus Interface\n");
        pthread_create(&thr_mpreader, NULL, &modbus_reader, (void*)&mpipe_args);
        pthread_create(&thr_mpwriter, NULL, &modbus_writer, (void*)&mpipe_args);
        pthread_create(&thr_mpparser, NULL, &modbus_parser, (void*)&mpipe_args);
    }
    else if (OTTER_FEATURE(MPIPE) && (cliopt_getintf() == INTF_mpipe)) {
        DEBUG_PRINTF("Opening Mpipe Interface\n");
        pthread_create(&thr_mpreader, NULL, &mpipe_reader, (void*)&mpipe_args);
        pthread_create(&thr_mpwriter, NULL, &mpipe_writer, (void*)&mpipe_args);
        pthread_create(&thr_mpparser, NULL, &mpipe_parser, (void*)&mpipe_args);
    }
    else {
        DEBUG_PRINTF("No active interface is available\n");
        goto otter_main_TERM2;
    }
    
    pthread_create(&thr_dterm, NULL, dterm_fn, (void*)&dterm_args);
    DEBUG_PRINTF("Finished creating theads\n");
    
    /// Threads are now running.  The rest of the main() code, below, is
    /// blocked by pthread_cond_wait() until the kill_cond is sent by one of 
    /// the child threads.  This will cause the program to quit.
    pthread_mutex_lock(&cli.kill_mutex);
    pthread_cond_wait(&cli.kill_cond, &cli.kill_mutex);
    
    DEBUG_PRINTF("Cancelling Theads\n");
    pthread_cancel(thr_mpreader);
    pthread_cancel(thr_mpwriter);
    pthread_cancel(thr_mpparser);
    pthread_cancel(thr_dterm);
    
    otter_main_TERM:
    DEBUG_PRINTF("Destroying thread resources\n");
    pthread_mutex_unlock(&dtwrite_mutex);
    pthread_mutex_destroy(&dtwrite_mutex);
    DEBUG_PRINTF("-- dtwrite_mutex destroyed\n");
    pthread_mutex_unlock(&rlist_mutex);
    pthread_mutex_destroy(&rlist_mutex);
    DEBUG_PRINTF("-- rlist_mutex destroyed\n");
    pthread_mutex_unlock(&tlist_mutex);
    pthread_mutex_destroy(&tlist_mutex);
    DEBUG_PRINTF("-- tlist_mutex destroyed\n");
    pthread_mutex_unlock(&tlist_cond_mutex);
    pthread_mutex_destroy(&tlist_cond_mutex);
    pthread_cond_destroy(&tlist_cond);
    DEBUG_PRINTF("-- tlist_mutex & tlist_cond destroyed\n");
    pthread_mutex_unlock(&pktrx_mutex);
    pthread_mutex_destroy(&pktrx_mutex);
    pthread_cond_destroy(&pktrx_cond);
    
    
    /// Close the drivers/files and free all allocated data objects (primarily 
    /// in mpipe).
    if (pipe == false) {
        DEBUG_PRINTF("Closing DTerm\n");
        dterm_close(&dterm);
    }
    
    otter_main_TERM2:
    DEBUG_PRINTF("Closing MPipe\n");
    mpipe_close(&mpipe_ctl);
    DEBUG_PRINTF("Freeing DTerm and Command History\n");
    dterm_free(&dterm);
    ch_free(&cmd_history);
    
    otter_main_TERM1:
    DEBUG_PRINTF("Freeing Mpipe Packet Lists\n");
    mpipe_freelists(&mpipe_rlist, &mpipe_tlist);
    DEBUG_PRINTF("Freeing PPipe lists\n");
    ppipelist_deinit();
    user_deinit();
    
    // cli.exitcode is set to 0, unless sigint is raised.
    DEBUG_PRINTF("Exiting cleanly and flushing output buffers\n");
    fflush(stdout);
    fflush(stderr);
    
    ///@todo there is a SIGILL that happens on pthread_cond_destroy(), but only
    ///      after packets have been TX'ed.
    ///      - Happens on two different systems
    ///      - May need to use valgrind to figure out what is happening
    ///      - after fixed, can move this code block upwards.
    DEBUG_PRINTF("-- rlist_mutex & rlist_cond destroyed\n");
    pthread_mutex_unlock(&cli.kill_mutex);
    DEBUG_PRINTF("-- pthread_mutex_unlock(&cli.kill_mutex)\n");
    pthread_mutex_destroy(&cli.kill_mutex);
    DEBUG_PRINTF("-- pthread_mutex_destroy(&cli.kill_mutex)\n");
    pthread_cond_destroy(&cli.kill_cond);
    DEBUG_PRINTF("-- cli.kill_mutex & cli.kill_cond destroyed\n");
    
    return cli.exitcode;
}



void sub_json_loadargs(cJSON* json, 
                       char* ttyfile, 
                       int* baudrate_val, 
                       bool* pipe_val,
                       int* intf_val,
                       char* xpath,
                       int* enc_bits,
                       int* enc_parity,
                       int* enc_stop,
                       bool* verbose_val ) {

#   define GET_STRINGENUM_ARG(DST, FUNC, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                *DST = FUNC(arg->valuestring); \
            }   \
        }   \
    } while(0)
                       
#   define GET_STRING_ARG(DST, LIMIT, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                strncpy(DST, arg->valuestring, LIMIT);   \
            }   \
        }   \
    } while(0)
    
#   define GET_CHAR_ARG(DST, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                *DST = arg->valuestring[0]; \
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

    GET_BOOL_ARG(pipe_val, "pipe");
    
    GET_STRINGENUM_ARG(intf_val, sub_intf_cmp, "intf");
    
    GET_STRING_ARG(xpath, 256, "xpath");
    
    GET_INT_ARG(enc_bits, "tty_bits");
    GET_CHAR_ARG(enc_parity, "tty_parity");
    GET_INT_ARG(enc_stop, "tty_stopbits");

    GET_BOOL_ARG(verbose_val, "verbose");
}






void ppipelist_populate(cJSON* obj) {

    if (obj != NULL) {
        const char* prefix;
        prefix = obj->string;
        
        obj = obj->child;
        while (obj != NULL) { 
            if (cJSON_IsString(obj) != 0) { 
                //fprintf(stderr, "%s, %s, %s\n", prefix, obj->string, obj->valuestring);
                ppipelist_new(prefix, obj->string, obj->valuestring); 
                //fprintf(stderr, "%d\n", __LINE__);
            }
            obj = obj->next;
        }
    }
}


 
