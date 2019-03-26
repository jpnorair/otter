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

// Top Level otter application headers
#include "otter_app.h"

// Application Sub-Headers
#include "cliopt.h"
#include "cmd_api.h"
#include "cmdhistory.h"
#include "debug.h"
#include "devtable.h"

#include "modbus.h"
#include "mpipe.h"
#include "user.h"

// DTerm to be extricated into its own library
#include "dterm.h"

// Potentially deprecated, to be replaced with sockets
#include "ppipe.h"
#include "ppipelist.h"

// Local Libraries
#include <argtable3.h>
#include <cJSON.h>
#include <cmdtab.h>
#if OTTER_FEATURE(MODBUS)
#   include <smut.h>
#endif

// Package Libraries
#include <talloc.h>

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


// tty specifier data type
typedef struct {
    char* ttyfile;
    int baudrate;
    int enc_bits;
    int enc_parity;
    int enc_stopbits;
} ttyspec_t;



// CLI Data Type deals with kill signals
typedef struct {
    int             exitcode;
    pthread_mutex_t kill_mutex;
    pthread_cond_t  kill_cond;
} cli_struct;

static cliopt_t cliopts;
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
    cli.exitcode = -1;
    pthread_cond_signal(&cli.kill_cond);
}

void sigquit_handler(int sigcode) {
    cli.exitcode = 0;
    pthread_cond_signal(&cli.kill_cond);
}


ttyspec_t* otter_ttylist_init(size_t size);

char* otter_ttylist_add(ttyspec_t* ttylist_item, const char* path);

void otter_ttylist_free(ttyspec_t* ttylist, size_t size);

int otter_main( ttyspec_t* ttylist,
                size_t num_tty,
                INTF_Type intf,
                char* socket,
                bool quiet,
                const char* initfile,
                const char* xpath,
                cJSON* params
                ); 

void otter_json_loadargs(cJSON* json,
                       ttyspec_t** ttylist,
                       int* num_tty,
                       int* io_val,
                       int* fmt_val,
                       int* intf_val,
                       char** socket_val,
                       bool* quiet_val,
                       char** initfile,
                       char** xpath,
                       bool* verbose_val );

void ppipelist_populate(cJSON* obj);









static IO_Type sub_io_cmp(const char* s1) {
    IO_Type selected_io;

    if (strcmp(s1, "modbus") == 0) {
        selected_io = IO_modbus;
    }
    else if (0) {
        ///@todo add future interfaces here
    }
    else {
        selected_io = IO_mpipe;
    }
    
    return selected_io;
}

static INTF_Type sub_intf_cmp(const char* s1) {
    INTF_Type selected_intf;

    if (strcmp(s1, "pipe") == 0) {
        selected_intf = INTF_pipe;
    }
    else if (strcmp(s1, "socket") == 0) {
        selected_intf = INTF_socket;
    }
    else {
        selected_intf = INTF_interactive;
    }
    
    return selected_intf;
}

static FORMAT_Type sub_fmt_cmp(const char* s1) {
    FORMAT_Type selected_fmt;
    
    if (strcmp(s1, "default") == 0) {
        selected_fmt = FORMAT_Default;
    }
    else if (strcmp(s1, "json") == 0) {
        selected_fmt = FORMAT_Json;
    }
    else if (strcmp(s1, "bintex") == 0) {
        selected_fmt = FORMAT_Bintex;
    }
    else if (strcmp(s1, "hex") == 0) {
        selected_fmt = FORMAT_Hex;
    }
    else {
        selected_fmt = FORMAT_Default;
    }
    
    return selected_fmt;
}






int main(int argc, char* argv[]) {
/// ArgTable params: These define the input argument behavior
#   define FILL_STRINGARG(ARGITEM, VAR)   do { \
        size_t str_sz = strlen(ARGITEM->filename[0]) + 1;   \
        if (VAR != NULL) free(VAR);                         \
        VAR = malloc(str_sz);                               \
        if (VAR == NULL) goto main_FINISH;                  \
        memcpy(VAR, ARGITEM->filename[0], str_sz);          \
    } while(0);

    struct arg_file *ttyfile = arg_file1(NULL,NULL,"ttyfile",           "Path to tty file (e.g. /dev/tty.usbmodem)");
    struct arg_int  *brate   = arg_int0(NULL,NULL,"baudrate",           "Baudrate, default is 115200");
    struct arg_str  *ttyenc  = arg_str0("e", "encoding", "ttyenc",      "Manual-entry for TTY encoding (default mpipe:8N1, modbus:8N2)");
    struct arg_str  *iobus   = arg_str0("b", "bus", "mpipe|modbus",      "Select \"mpipe\" or \"modbus\" bus (default=mpipe)");
    struct arg_str  *fmt     = arg_str0("f", "fmt", "format",           "\"default\", \"json\", \"bintex\", \"hex\"");
    struct arg_str  *intf    = arg_str0("i","intf", "interactive|pipe|socket", "Interface select.  Default: interactive");
    struct arg_file *socket  = arg_file0("S","socket","path/addr",      "Socket path/address to use for otter daemon");
    struct arg_file *initfile= arg_file0("I","init","path",             "Path to initialization routine to run at startup");
    struct arg_file *xpath   = arg_file0("x", "xpath", "path",          "Path to directory of external data processor programs");
    //struct arg_str  *parsers = arg_str1("p", "parsers", "<msg:parser>", "parser call string with comma-separated msg:parser pairs");
    //struct arg_str  *fparse  = arg_str1("P", "parsefile", "<file>",     "file containing comma-separated msg:parser pairs");
    // Generic
    struct arg_file *config  = arg_file0("c","config","file.json",      "JSON based configuration file.");
    struct arg_lit  *verbose = arg_lit0("v","verbose",                  "Use verbose mode");
    struct arg_lit  *debug   = arg_lit0("d","debug",                    "Set debug mode on: requires compiling for debug");
    struct arg_lit  *quiet   = arg_lit0("q","quiet",                    "Supress reporting of errors");
    struct arg_lit  *help    = arg_lit0(NULL,"help",                    "Print this help and exit");
    struct arg_lit  *version = arg_lit0(NULL,"version",                 "Print version information and exit");
    struct arg_end  *end     = arg_end(20);
    
    void* argtable[] = { ttyfile, brate, ttyenc, iobus, fmt, intf, socket, initfile, xpath, config, verbose, debug, quiet, help, version, end };
    const char* progname = OTTER_PARAM(NAME);
    int nerrors;
    bool bailout        = true;
    int exitcode        = 0;
    int num_tty         = 0;
    ttyspec_t* ttylist  = NULL;
    char* initfile_val  = NULL;
    char* xpath_val     = NULL;
    cJSON* json         = NULL;
    char* buffer        = NULL;
    IO_Type io_val    = OTTER_FEATURE(MPIPE) ? IO_mpipe : IO_modbus;
    FORMAT_Type fmt_val = FORMAT_Default;
    INTF_Type intf_val  = INTF_interactive;
    char* socket_val    = NULL;
    bool quiet_val      = false;
    bool verbose_val    = false;

    if (arg_nullcheck(argtable) != 0) {
        /// NULL entries were detected, some allocations must have failed 
        fprintf(stderr, "%s: insufficient memory\n", progname);
        exitcode=1;
        goto main_FINISH;
    }

    /* Parse the command line as defined by argtable[] */
    nerrors = arg_parse(argc, argv, argtable);

    /// special case: '--help' takes precedence over error reporting
    if (help->count > 0) {
        printf("Usage: %s", progname);
        arg_print_syntax(stdout, argtable, "\n");
        arg_print_glossary(stdout, argtable, "  %-25s %s\n");
        
        exitcode = 0;
        goto main_FINISH;
    }

    /// special case: '--version' takes precedence error reporting 
    if (version->count > 0) {
        printf("%s -- %s\n", OTTER_PARAM_VERSION, OTTER_PARAM_DATE);
        printf("Commit-ID: %s\n", OTTER_PARAM_GITHEAD);
        printf("Designed by %s\n", OTTER_PARAM_BYLINE);
        printf("Based on otter by JP Norair (indigresso.com)\n");
        
        exitcode = 0;
        goto main_FINISH;
    }

    /// If the parser returned any errors then display them and exit
    /// - Display the error details contained in the arg_end struct.
    if (nerrors > 0) {
        arg_print_errors(stdout,end,progname);
        printf("Try '%s --help' for more information.\n", progname);
        
        exitcode = 1;
        goto main_FINISH;
    }

    /// special case: with no command line options induces brief help 
    if (argc==1) {
        printf("Try '%s --help' for more information.\n",progname);
        
        exitcode = 0;
        goto main_FINISH;
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
            goto main_FINISH;
        }

        fseek(fp, 0L, SEEK_END);
        lSize = ftell(fp);
        rewind(fp);

        buffer = calloc(1, lSize+1);
        if (buffer == NULL) {
            exitcode = (int)'m';
            goto main_FINISH;
        }

        if(fread(buffer, lSize, 1, fp) == 1) {
            json = cJSON_Parse(buffer);
            fclose(fp);
        }
        else {
            fclose(fp);
            fprintf(stderr, "read to %s fails\n", config->filename[0]);
            exitcode = (int)'r';
            goto main_FINISH;
        }

        /// At this point the file is closed and the json is parsed into the
        /// "json" variable.  
        if (json == NULL) {
            fprintf(stderr, "JSON parsing failed.  Exiting.\n");
            goto main_FINISH;
        }
        
        {   int tmp_io   = (int)io_val;
            int tmp_fmt  = (int)fmt_val;
            int tmp_intf = (int)intf_val;
            
            otter_json_loadargs(  json,
                                &ttylist,
                                &num_tty,
                                &tmp_io,
                                &tmp_fmt,
                                &tmp_intf,
                                &socket_val,
                                &quiet_val,
                                &initfile_val,
                                &xpath_val,
                                &verbose_val
                            );
            io_val   = tmp_io;
            fmt_val  = tmp_fmt;
            intf_val = tmp_intf;
        }
    }
    
    /// Arguments through the command line take precedence over JSON
    if (ttyfile->count != 0) {
        otter_ttylist_free(ttylist, num_tty);
        
        num_tty = 1;
        ttylist = otter_ttylist_init(1);
        if (ttylist == NULL) {
            printf("Initialization error: ttylist not created.\n");
            bailout = true;
            goto main_FINISH;
        }
        
        otter_ttylist_add(&ttylist[0], ttyfile->filename[0]);
        ttylist[0].baudrate     = brate->count ? brate->ival[0] : OTTER_PARAM_DEFBAUDRATE;
        ttylist[0].enc_bits     = 8;
        ttylist[0].enc_parity   = (int)'N';
        ttylist[0].enc_stopbits = 1;
        
        if (ttyenc->count != 0) {
            int str_sz = (int)strlen(ttyenc->sval[0]);
            if (str_sz > 0) ttylist[0].enc_bits     = (int)(ttyenc->sval[0][0]-'0');
            if (str_sz > 1) ttylist[0].enc_parity   = (int)ttyenc->sval[0][1];
            if (str_sz > 1) ttylist[0].enc_stopbits = (int)(ttyenc->sval[0][2]-'0');
        }
    }
    if (ttylist == NULL) {
        printf("Input error: no tty provided\n");
        printf("Try '%s --help' for more information.\n", progname);
        bailout = true;
    }
    
    if (iobus->count != 0) {
        io_val = sub_io_cmp(iobus->sval[0]);
    }
    if (fmt->count != 0) {
        fmt_val = sub_fmt_cmp(fmt->sval[0]);
    }
    if (intf->count != 0) {
        intf_val = sub_intf_cmp(intf->sval[0]);
    }
    if (socket->count != 0) {
        FILL_STRINGARG(socket, socket_val);
    }
    if (quiet->count != 0) {
        quiet_val = true;
    }
    if (initfile->count != 0) {
        FILL_STRINGARG(initfile, initfile_val);
    }
    if (xpath->count != 0) {
        FILL_STRINGARG(xpath, xpath_val);
    }
    if (verbose->count != 0) {
        verbose_val = true;
    }

    // override interface value if socket address is provided
    if (socket_val != NULL) {
        intf_val = INTF_socket;
    }

    /// Client Options.  These are read-only from internal modules
    cliopts.io          = io_val;
    cliopts.format      = fmt_val;
    cliopts.verbose_on  = verbose_val;
    cliopts.debug_on    = (debug->count != 0) ? true : false;
    cliopts.quiet_on    = quiet_val;
    cliopt_init(&cliopts);

    /// All configuration is done.
    /// Send all configuration data to program main function.
    bailout = false;
    
    /// Final value checks
    main_FINISH:

    /// Free un-necessary resources
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    
    /// Run otter if no issues
    if (bailout == false) {
        exitcode = otter_main(  ttylist,
                                num_tty,
                                intf_val,
                                socket_val,
                                quiet_val,
                                (const char*)initfile_val,
                                (const char*)xpath_val,
                                json    );
    }

    /// Free all data that was needed by the otter main program
    otter_ttylist_free(ttylist, num_tty);

    cJSON_Delete(json);

    free(socket_val);
    free(xpath_val);
    free(initfile_val);
    free(buffer);

    return exitcode;
}



ttyspec_t* otter_ttylist_init(size_t size) {
    ttyspec_t* ttylist;
    ttylist = calloc(size, sizeof(ttyspec_t));
    return ttylist;
}

char* otter_ttylist_add(ttyspec_t* ttylist_item, const char* path) {
    if (ttylist_item != NULL) {
        ttylist_item->ttyfile = calloc(strlen(path)+1, sizeof(char));
        if (ttylist_item->ttyfile != NULL) {
            strcpy(ttylist_item->ttyfile, path);
            return ttylist_item->ttyfile;
        }
    }
    return NULL;
}

void otter_ttylist_free(ttyspec_t* ttylist, size_t size) {
    if (ttylist != NULL) {
        while (size != 0) {
            size--;
            if (ttylist[size].ttyfile != NULL) {
                free(ttylist[size].ttyfile);
            }
        }
    }
}


///@todo copy initialization of dterm from otdb_main()
/// What this should do is start two threads, one for the character I/O on
/// the dterm side, and one for the serial I/O.
int otter_main( ttyspec_t* ttylist,
                size_t num_tty,
                INTF_Type intf,
                char* socket,
                bool quiet,
                const char* initfile,
                const char* xpath,
                cJSON* params) {    
    
    int rc;
    
    
    // MPipe Datastructs
    pktlist_t   mpipe_tlist;
    pktlist_t   mpipe_rlist;
    
    // DTerm Datastructs
    dterm_handle_t dterm_handle;
    
    // Thread & Mutex Instances
    ///@todo do tlist_mutex, tlist_cond need instantiation here?
    pthread_mutex_t     tlist_mutex;
    pthread_cond_t      tlist_cond;
    pthread_mutex_t     rlist_mutex;
    pthread_mutex_t     tlist_cond_mutex;
    pthread_cond_t      pktrx_cond;
    pthread_mutex_t     pktrx_mutex;
    pthread_t           thr_mpreader;
    pthread_t           thr_mpwriter;
    pthread_t           thr_mpparser;
    void*               (*dterm_fn)(void* args);
    pthread_t           thr_dterm;
    
    // Application data container
    otter_app_t appdata;
    
    // 0 is the success code
    cli.exitcode = 0;
    
    /// Initialize Otter Environment Variables.
    /// This must be the first module to be initialized.
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.

    /// Device Table initialization.
    /// Device 0 is used for implicit addressing
    
    // -----------------------------------------------------------------------
    /// Initialize the ppipe system of named pipes, based on the input json
    /// configuration file.  We have input and output pipes of several types.
    /// @todo Pipelist is deprecated, and to be replaced by sockets
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
    // -----------------------------------------------------------------------
    
    /// Initialize Thread Mutexes & Conds.  This is finnicky and it must be
    /// done before assignment into the argument containers.
    DEBUG_PRINTF("Initializing Multitasking elements ...\n");
    assert( pthread_mutex_init(&cli.kill_mutex, NULL) == 0 );
    pthread_cond_init(&cli.kill_cond, NULL);
    assert( pthread_mutex_init(&rlist_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&tlist_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&tlist_cond_mutex, NULL) == 0 );
    pthread_cond_init(&tlist_cond, NULL);
    assert( pthread_mutex_init(&pktrx_mutex, NULL) == 0 );
    pthread_cond_init(&pktrx_cond, NULL);
    DEBUG_PRINTF("--> done\n");
    
    /// Initialize DTerm data objects
    /// Non intrinsic dterm elements (cmdtab, devtab, etc) get attached
    /// following initialization
    DEBUG_PRINTF("Initializing DTerm ...\n");
    if (dterm_init(&dterm_handle, &appdata, intf) != 0) {
        cli.exitcode = 1;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");
    
    /// Initialize command search table.
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.
    DEBUG_PRINTF("Initializing commands ...\n");
    if (cmd_init(&appdata.cmdtab, xpath) < 0) {
        fprintf(stderr, "Err: command table cannot be initialized.\n");
        cli.exitcode = 2;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");
    
    /// Initialize Device Table
    DEBUG_PRINTF("Initializing Device Table ...\n");
    rc = devtab_init(&appdata.endpoint.devtab);
    if (rc != 0) {
        fprintf(stderr, "Device Table Initialization Failure (%i)\n", rc);
        cli.exitcode = 3;
        goto otter_main_EXIT;
    }
    rc = devtab_insert(appdata.endpoint.devtab, 0, 0, NULL, NULL, NULL);
    if (rc != 0) {
        fprintf(stderr, "Device Table Insertion Failure (%i)\n", rc);
        cli.exitcode = 4;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");
    
    /// Subscriber initialization.
    DEBUG_PRINTF("Initializing Subscribers ...\n");
    rc = subscriber_init(&appdata.subscribers);
    if (rc != 0) {
        fprintf(stderr, "Subscribers Initialization Failure (%i)\n", rc);
        cli.exitcode = 5;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");

    /// Initialize the user manager.
    /// This must be done prior to command module init
    DEBUG_PRINTF("Initializing Users ...\n");
    rc = user_init();
    if (rc != 0) {
        fprintf(stderr, "User Construct Initialization Failure (%i)\n", rc);
        cli.exitcode = 6;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");

    /// Initialize packet lists for transmitted packets and received packets
    DEBUG_PRINTF("Initializing Packet Lists ...\n");
    if ((pktlist_init(&mpipe_rlist) != 0)
    ||  (pktlist_init(&mpipe_tlist) != 0)) {
        fprintf(stderr, "Pktlist Initialization Failure (%i)\n", -1);
        cli.exitcode = 7;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");

    /// Initialize mpipe memory
    DEBUG_PRINTF("Initializing MPipe ...\n");
    rc = mpipe_init(&appdata.mpipe, num_tty);
    if (rc != 0) {
        fprintf(stderr, "MPipe Initialization Failure (%i)\n", rc);
        cli.exitcode = 8;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");
    
    /// Initialize Smut.  This is only used with Modbus IO
    appdata.smut_handle = NULL;
#   if OTTER_FEATURE(MODBUS)
    if (cliopt_getio() == IO_modbus) {
        DEBUG_PRINTF("Initializing SMUT ...\n");
        appdata.smut_handle = smut_init();
        DEBUG_PRINTF("--> done\n");
    }
#   endif

    /// Link Remaining object data into the Application container
    /// Link threads variable instances into appdata
    ///@todo endpoint should be a pointer
    appdata.tlist          = &mpipe_tlist;
    appdata.rlist          = &mpipe_rlist;
    appdata.endpoint.node  = devtab_select(appdata.endpoint.devtab, 0);
    appdata.endpoint.usertype= USER_guest;
    appdata.dterm_parent   = &dterm_handle;
    appdata.tlist_mutex    = &tlist_mutex;
    appdata.tlist_cond     = &tlist_cond;
    appdata.rlist_mutex    = &rlist_mutex;
    appdata.tlist_cond_mutex = &tlist_cond_mutex;
    appdata.pktrx_cond     = &pktrx_cond;
    appdata.pktrx_mutex    = &pktrx_mutex;

    /// Open the mpipe TTY & Setup MPipe threads
    /// The MPipe Filename (e.g. /dev/ttyACMx) is sent as the first argument
    DEBUG_PRINTF("Opening MPipe Interfaces ...\n");
    for (int i=0; i<num_tty; i++) {
        int open_rc;
        open_rc = mpipe_opentty(appdata.mpipe, i,
                                ttylist[i].ttyfile,
                                ttylist[i].baudrate,
                                ttylist[i].enc_bits,
                                ttylist[i].enc_parity,
                                ttylist[i].enc_stopbits,
                                0, 0, 0);
        if (open_rc < 0) {
            fprintf(stderr, "Could not open TTY on %s (error %i)\n", ttylist[i].ttyfile, open_rc);
            cli.exitcode = 9;
            goto otter_main_EXIT;
        }
    }
    DEBUG_PRINTF("--> done\n");
    
    /// Open DTerm interface & Setup DTerm threads
    /// If sockets are not used, by design socket_path will be NULL.
    DEBUG_PRINTF("Opening DTerm on %s ...\n", socket);
    dterm_fn = dterm_open(appdata.dterm_parent, socket);
    if (dterm_fn == NULL) {
        cli.exitcode = 10;
        goto otter_main_EXIT;
    }
    DEBUG_PRINTF("--> done\n");
    
    // -----------------------------------------------------------------------
    DEBUG_PRINTF("Finished setup of otter modules. Now creating app threads.\n");
    // -----------------------------------------------------------------------
    
    /// Invoke the child threads below.  All of the child threads run
    /// indefinitely until an error occurs or until the user quits.  Quit can 
    /// be via Ctl+C or Ctl+\, or potentially also through a dterm command.  
    /// Each thread must be be implemented to raise SIGQUIT or SIGINT on exit
    /// i.e. raise(SIGINT).
    DEBUG_PRINTF("Creating MPipe theads\n");
    if (appdata.mpipe != NULL) {
#       if OTTER_FEATURE(MODBUS)
        if (cliopt_getio() == IO_modbus) {
            DEBUG_PRINTF("Opening Modbus Interface\n");
            ///@todo have a function here that returns a handle, and the handle
            ///      is also what's deallocated.  Tie together with mpipe_open().
            pthread_create(&thr_mpreader, NULL, &modbus_reader, (void*)&appdata);
            pthread_create(&thr_mpwriter, NULL, &modbus_writer, (void*)&appdata);
            pthread_create(&thr_mpparser, NULL, &modbus_parser, (void*)&appdata);
        } else
#       endif
#       if OTTER_FEATURE(MPIPE)
        if (cliopt_getio() == IO_mpipe) {
            DEBUG_PRINTF("Opening Mpipe Interface\n");
            ///@todo have a function here that returns a handle, and the handle
            ///      is also what's deallocated.  Tie together with mpipe_open().
            pthread_create(&thr_mpreader, NULL, &mpipe_reader, (void*)&appdata);
            pthread_create(&thr_mpwriter, NULL, &mpipe_writer, (void*)&appdata);
            pthread_create(&thr_mpparser, NULL, &mpipe_parser, (void*)&appdata);
        } else
#       endif
        {
            fprintf(stderr, "Specified interface (id:%i) not supported\n", cliopt_getio());
            cli.exitcode = 11;
            goto otter_main_EXIT;
        }
    }
    
    /// Initialize the signal handlers for this process.
    /// These are activated by Ctl+C (SIGINT) and Ctl+\ (SIGQUIT) as is
    /// typical in POSIX apps.  When activated, the threads are halted and
    /// Otter is shutdown.
    DEBUG_PRINTF("Assign Kill Signals\n");
    _assign_signal(SIGINT, &sigint_handler);
    _assign_signal(SIGQUIT, &sigquit_handler);
    DEBUG_PRINTF("--> done\n");
    
    /// Before doing any interactions, run a command file if it exists.
    DEBUG_PRINTF("Running Initialization Command File\n");
    if (initfile != NULL) {
        //VERBOSE_PRINTF("Running init file: %s\n", initfile);
        if (dterm_cmdfile(appdata.dterm_parent, initfile) < 0) {
            //fprintf(stderr, ERRMARK"Could not run initialization file.\n");
            fprintf(stderr, "Could not run initialization file.\n");
        }
        else {
            //VERBOSE_PRINTF("Init file finished successfully\n");
        }
    }
    DEBUG_PRINTF("--> done\n");
    
    DEBUG_PRINTF("Creating Dterm theads\n");
    pthread_create(&thr_dterm, NULL, dterm_fn, (void*)&dterm_handle);
    DEBUG_PRINTF("--> done\n");
    
// ----------------------------------------------------------------------------
    
    /// Threads are now running.  The rest of the main() code, below, is
    /// blocked by pthread_cond_wait() until the kill_cond is sent by one of
    /// the child threads.  This will cause the program to quit.
    
// ----------------------------------------------------------------------------
    
    /// Wait for kill cond.  Basic functionality is in the dterm thread(s).
    pthread_mutex_lock(&cli.kill_mutex);
    pthread_cond_wait(&cli.kill_cond, &cli.kill_mutex);
    
    DEBUG_PRINTF("Cancelling Theads\n");
    pthread_detach(thr_dterm);
    pthread_cancel(thr_dterm);
    
    if (appdata.mpipe != NULL) {
        pthread_cancel(thr_mpreader);
        pthread_cancel(thr_mpwriter);
        pthread_cancel(thr_mpparser);
    }
    
    /// Close the drivers/files and deinitialize all submodules
    otter_main_EXIT:
    switch (cli.exitcode) {
       default:
        case 11:// Failure in MPipe thread creation
        case 10:// Failure on dterm_open()
        case 9: // Failure on mpipe_opentty()
                DEBUG_PRINTF("Deinitializing MPipe\n");
                mpipe_deinit(appdata.mpipe);
#               if OTTER_FEATURE(MODBUS)
                if (cliopt_getio() == IO_modbus) {
                    smut_free(appdata.smut_handle);
                }
#               endif
       
        case 8: // Failure on mpipe_init()
                DEBUG_PRINTF("Deinitializing Packet Lists\n");
                pktlist_free(&mpipe_rlist);
                pktlist_free(&mpipe_tlist);
        
        case 7: // Failure on pktlist_init()
                DEBUG_PRINTF("Deinitializing User Module\n");
                user_deinit();
       
        case 6: // Failure on user_init()
                DEBUG_PRINTF("Deinitializing Subscribers\n");
                subscriber_deinit(appdata.subscribers);
            
        case 5: // Failure on subscriber_init()
        case 4: // Failure on devtab_insert()
                DEBUG_PRINTF("Deinitializing Device Table\n");
                devtab_free(appdata.endpoint.devtab);
        
        case 3: // Failure on devtab_init()
                DEBUG_PRINTF("Deinitializing Command Table\n");
                cmd_free(appdata.cmdtab);
        
        case 2: // Failure on cmd_init()
                dterm_deinit(&dterm_handle);
        
        case 1: // Failure on dterm_init()
                ppipelist_deinit();
                break;
    }
    
    DEBUG_PRINTF("Destroying thread resources\n");
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




void otter_json_loadargs(cJSON* json,
                       ttyspec_t** ttylist,
                       int* num_tty,
                       int* io_val,
                       int* fmt_val,
                       int* intf_val,
                       char** socket_val,
                       bool* quiet_val,
                       char** initfile,
                       char** xpath,
                       bool* verbose_val ) {
    
#   define GET_STRINGENUM_ARG(DST, FUNC, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                *DST = FUNC(arg->valuestring); \
            }   \
        }   \
    } while(0)
                       
#   define GET_STRING_ARG(DST, NAME) do { \
        arg = cJSON_GetObjectItem(json, NAME);  \
        if (arg != NULL) {  \
            if (cJSON_IsString(arg) != 0) {    \
                size_t sz = strlen(arg->valuestring)+1; \
                DST = malloc(sz);   \
                if (DST != NULL) memcpy(DST, arg->valuestring, sz); \
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
    
    cJSON* ttyargs;
    cJSON* arg;
    ttyspec_t* ttys;
    
    ///1. Get "arguments" object, if it exists
    json = cJSON_GetObjectItem(json, "arguments");
    if (json == NULL) {
        return;
    }
    
    /// 2. Get the TTY arguments.  This is different than other arguments.
    ttyargs = cJSON_GetObjectItem(json, "ttylist");
    if (cJSON_IsArray(ttyargs)) {
        *num_tty    = cJSON_GetArraySize(ttyargs);
        ttys        = otter_ttylist_init(*num_tty);
        *ttylist    = ttys;
        if (ttys != NULL) {
            for (int i=0; i<*num_tty; i++) {
                cJSON* obj;
                obj = cJSON_GetArrayItem(ttyargs, i);
                if (cJSON_IsObject(obj)) {
                    arg = cJSON_GetObjectItem(obj, "tty");
                    ttys[i].ttyfile = cJSON_IsString(arg) ? \
                        otter_ttylist_add(&ttys[i], cJSON_GetStringValue(arg)) : NULL;
                    
                    ///@todo Defaults can be handled better
                    arg = cJSON_GetObjectItem(obj, "baudrate");
                    ttys[i].baudrate = cJSON_IsNumber(arg) ? (int)arg->valueint : OTTER_PARAM_DEFBAUDRATE;

                    arg = cJSON_GetObjectItem(obj, "bits");
                    ttys[i].enc_bits = cJSON_IsNumber(arg) ? (int)arg->valueint : 8;
                    
                    arg = cJSON_GetObjectItem(obj, "parity");
                    ttys[i].enc_parity = cJSON_IsString(arg) ? arg->valuestring[0] : 'N';
                    
                    arg = cJSON_GetObjectItem(obj, "stopbits");
                    ttys[i].enc_stopbits = cJSON_IsNumber(arg) ? (int)arg->valueint : 1;
                }
            }
        }
    }
    
    /// 3. Systematically get all of the Normal arguments
    GET_STRINGENUM_ARG(io_val, sub_io_cmp, "io");
    GET_STRINGENUM_ARG(fmt_val, sub_fmt_cmp, "fmt");
    GET_STRINGENUM_ARG(intf_val, sub_intf_cmp, "intf");
    
    GET_BOOL_ARG(quiet_val, "quiet");
    GET_STRING_ARG(*socket_val, "socket");
    GET_STRING_ARG(*initfile, "init");
    GET_STRING_ARG(*xpath, "xpath");
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


 
