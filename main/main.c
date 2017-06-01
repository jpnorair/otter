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
#include "cmdsearch.h"
#include "cmdhistory.h"


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

void _print_usage(const char* program_name) {
    fprintf(stderr, "Usage: %s tty_file [baudrate]\n", program_name);
    fprintf(stderr, "       (Default Baudrate is %d baud)\n", _DEFAULT_BAUDRATE);
}




int main(int argc, const char * argv[]) {
/// What this should do is start two threads, one for the character I/O on
/// the dterm side, and one for the serial I/O.
    
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
    
    
    /// 1. Load Arguments from Command-Line
    // Usage Error
    if ((argc < 2) || (argc > 4)) {
        _print_usage(argv[0]);
        return 0;
    }
    
    // Validate that we're looking at something like "/dev/tty.usb..."
    
    
    // Prepare the baud-rate of the MPipe TTY
    if (argc >= 3)  mpipe_ctl.baudrate = atoi(argv[2]);
    else            mpipe_ctl.baudrate = _DEFAULT_BAUDRATE;
    
    // final argument [optional] is external call string
    if (argc >= 4) {
        mpipe_args.external_call = argv[3];
    }
    else {
        mpipe_args.external_call = NULL;
    }

    /// 2. Initialize command search table.  
    ///@todo in the future, let's pull this from an initialization file or
    ///      something dynamic as such.
    cmdsearch_init(NULL);
    
    
    /// 3. Initialize packet lists for transmitted packets and received packets
    pktlist_init(&mpipe_rlist);
    pktlist_init(&mpipe_tlist);
    
    
    /// 4. Initialize Thread Mutexes & Conds.  This is finnicky and it must be
    ///    done before assignment into the argument containers, possibly due to 
    ///    C-compiler foolishly optimizing.
    assert( pthread_mutex_init(&dtwrite_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&rlist_mutex, NULL) == 0 );
    assert( pthread_mutex_init(&tlist_mutex, NULL) == 0 );
    
    assert( pthread_mutex_init(&cli.kill_mutex, NULL) == 0 );
    pthread_cond_init(&cli.kill_cond, NULL);
    assert( pthread_mutex_init(&tlist_cond_mutex, NULL) == 0 );
    pthread_cond_init(&tlist_cond, NULL);
    assert( pthread_mutex_init(&pktrx_mutex, NULL) == 0 );
    pthread_cond_init(&pktrx_cond, NULL);
    
    
    /// 5. Open the mpipe TTY & Setup MPipe threads
    ///    The MPipe Filename (e.g. /dev/ttyACMx) is sent as the first argument
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
    
    if (mpipe_open(&mpipe_ctl, argv[1], mpipe_ctl.baudrate, 8, 'N', 1, 0, 0, 0) < 0) {
        return -1;
    }
    
    /// 6. Open DTerm interface & Setup DTerm threads
    ///    The dterm thread will deal with all other aspects, such as command
    ///    entry and history initialization.
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
        return -1;
    }
    
    
    /// 7. Initialize the signal handlers for this process.
    ///    These are activated by Ctl+C (SIGINT) and Ctl+\ (SIGQUIT) as is
    ///    typical in POSIX apps.  When activated, the threads are halted and
    ///    Otter is shutdown.
    cli.exitcode = EXIT_SUCCESS;
    _assign_signal(SIGINT, &sigint_handler);
    _assign_signal(SIGQUIT, &sigquit_handler);
    
    
    /// 8. Invoke the child threads below.  All of the child threads run
    /// indefinitely until an error occurs or until the user quits.  Quit can 
    /// be via Ctl+C or Ctl+\, or potentially also through a dterm command.  
    /// Each thread must be be implemented to raise SIGQUIT or SIGINT on exit
    /// i.e. raise(SIGQUIT).
    pthread_create(&thr_mpreader, NULL, &mpipe_reader, (void*)&mpipe_args);
    pthread_create(&thr_mpwriter, NULL, &mpipe_writer, (void*)&mpipe_args);
    pthread_create(&thr_mpparser, NULL, &mpipe_parser, (void*)&mpipe_args);
    pthread_create(&thr_dtprompter, NULL, &dterm_prompter, (void*)&dterm_args);
    
    
    /// 9. Threads are now running.  The rest of the main() code, below, is
    ///    blocked by pthread_cond_wait() until the kill_cond is sent by one of 
    /// the child threads.  This will cause the program to quit.
    pthread_mutex_lock(&cli.kill_mutex);
    pthread_cond_wait(&cli.kill_cond, &cli.kill_mutex);
    pthread_cancel(thr_mpreader);
    pthread_cancel(thr_mpwriter);
    pthread_cancel(thr_mpparser);
    pthread_cancel(thr_dtprompter);
    
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
    
    
    /// 10. Close the drivers/files and free all allocated data objects
    ///     (primarily in mpipe).
    dterm_close(&dterm);
    mpipe_close(&mpipe_ctl);
    dterm_free(&dterm);
    mpipe_freelists(&mpipe_rlist, &mpipe_tlist);
    ch_free(&cmd_history);
    
    
    // cli.exitcode is set to 0, unless sigint is raised.
#   ifdef __TEST__
    fprintf(stderr, "Exiting Cleanly\n"); 
#   endif

    return cli.exitcode;
}


 
