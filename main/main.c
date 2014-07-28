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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
















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
    fprintf(stderr, "usage: %s tty_file [baudrate]\n", program_name);
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
    if ((argc < 2) || (argc > 3)) {
        _print_usage(argv[0]);
    }
    // Prepare the baud-rate of the MPipe TTY
    if (argc == 3)  mpipe_ctl.baudrate = atoi(argv[2]);
    else            mpipe_ctl.baudrate = 115200;
    
    
    /// 2. Open DTerm interface & Setup DTerm threads
    ///    The dterm thread will deal with all other aspects, such as command
    ///    entry and history initialization.
    ///@todo "STDIN_FILENO" and "STDOUT_FILENO" could be made dynamic
    _dtputs_dterm           = &dterm;
    dterm.fd_in             = STDIN_FILENO;
    dterm.fd_out            = STDOUT_FILENO;
    dterm_args.ch           = ch_init(&cmd_history);
    dterm_args.dt           = &dterm;
    dterm_args.kill_mutex   = &cli.kill_mutex;
    dterm_args.kill_cond    = &cli.kill_cond;
    if (dterm_open(&dterm) < 0) {
        return -1;
    }
    
    
    /// 3. Open the mpipe TTY & Setup MPipe threads
    ///    The MPipe Filename (e.g. /dev/ttyACMx) is sent as the first argument
    mpipe_args.mpctl            = &mpipe_ctl;
    mpipe_args.rlist            = &mpipe_tlist;
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
    
    
    /// 4. Initialize the signal handlers for this process.
    ///    These are activated by Ctl+C (SIGINT) and Ctl+\ (SIGQUIT) as is
    ///    typical in POSIX apps.  When activated, the threads are halted and
    ///    Otter is shutdown.
    cli.exitcode = EXIT_SUCCESS;
    _assign_signal(SIGINT, &sigint_handler);
    _assign_signal(SIGQUIT, &sigquit_handler);
    
    
    /// 5. Invoke the child threads below.  All of the child threads run 
    /// indefinitely until an error occurs or until the user quits.  Quit can 
    /// be via Ctl+C or Ctl+\, or potentially also through a dterm command.  
    /// Each thread must be be implemented to raise SIGQUIT or SIGINT on exit
    /// i.e. raise(SIGQUIT).
    
    pthread_mutex_init(&dtwrite_mutex, NULL);
    pthread_mutex_init(&rlist_mutex, NULL);
    pthread_mutex_init(&tlist_mutex, NULL);
    
    pthread_mutex_init(&cli.kill_mutex, NULL);
    pthread_cond_init(&cli.kill_cond, NULL);
    pthread_mutex_init(&tlist_cond_mutex, NULL);
    pthread_cond_init(&tlist_cond, NULL);
    pthread_mutex_init(&pktrx_mutex, NULL);
    pthread_cond_init(&pktrx_cond, NULL);

    pthread_create(&thr_mpreader, NULL, &mpipe_reader, (void*)&mpipe_args);
    pthread_create(&thr_mpwriter, NULL, &mpipe_writer, (void*)&mpipe_args);
    pthread_create(&thr_mpparser, NULL, &mpipe_parser, (void*)&mpipe_args);
    pthread_create(&thr_dtprompter, NULL, &dterm_prompter, (void*)&dterm_args);
    
    
    /// 6. Threads are now running.  The rest of the main() code, below, is 
    ///    blocked by pthread_cond_wait() until the kill_cond is sent by one of 
    /// the child threads.  This will cause the program to quit.
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
    
    
    /// 7. Close the drivers/files and free all allocated data objects 
    ///    (primarily in mpipe).
    dterm_close(&dterm);
    mpipe_close(&mpipe_ctl);
    dterm_free(&dterm);
    mpipe_freelists(&mpipe_rlist, &mpipe_tlist);
    ch_free(&cmd_history);
    
    // cli.exitcode is set to 0, unless sigint is raised.
    return cli.exitcode;
}














/* Original dterm Function by Sasha 
 
// This is for shell-like behavior
int tty_dterm(void) {
    dterm_t dt;
    
    if (dterm_setnoncan(&dt) < 0)
        return -1;
    
    cmdhist ch;
    ch_start(&ch);
    
    dterm_reset(&dt);
    dterm_puts(&dt, NAME);
    dterm_puts(&dt, INV);
    
    int pi;
    int ci;
    char *chp;
    char cmdname[CMD_NAMESIZE + 1];
    
    ///@todo have this use signals instead of endless read loop, or at least
    ///      some sort of thread blocking/waiting
    while(1) {
        switch (dterm_readcmd(&dt)) {
            case ct_simple:
                
                // track history
                if (*(dt.cmdbuf) && !ch_contains(&ch, dt.cmdbuf)) {
                    ch_add(&ch, dt.cmdbuf);
                }
                
                // search and try to execute cmd
                if ((pi = parsecmd(dt.cmdbuf, cmdname)) > -1 &&
                    (ci = srccmd(cmdname)) > -1) {
                    switch (commands[ci].method(dt.cmdbuf + pi)) {
                        case -1: dterm_puts(&dt, "command failed\n");    break;
                        case  1: dterm_puts(&dt, "command completed\n"); break;
                    }
                }
                else {
                    dterm_puts(&dt, "command not found\n");
                    dterm_puts(&dt, ASCII_BEL);
                }
                
                dterm_reset(&dt);
                dterm_puts(&dt, INV);
                break;
                
            case ct_autofill:
                
                // check whether command has entered w/o prms
                if ((pi = parsecmd(dt.cmdbuf, cmdname)) > -1 &&
                    *(dt.cmdbuf + pi) == 0 &&
                    (ci = srccmds(cmdname)) > -1) {
                    dterm_remln(&dt);
                    dterm_puts(&dt, INV);
                    dterm_putsc(&dt, commands[ci].name);
                    dterm_puts(&dt, commands[ci].name);
                }
                else {
                    dterm_puts(&dt, ASCII_BEL);
                }
                break;
                
            case ct_histnext:
                if (ch.count &&
                    (chp = ch_next(&ch))) {
                    dterm_remln(&dt);
                    dterm_puts(&dt, INV);
                    dterm_putsc(&dt, chp);
                    dterm_puts(&dt, chp);
                }
                break;
                
            case ct_histprev:
                if (ch.count &&
                    (chp = ch_prev(&ch))) {
                    dterm_remln(&dt);
                    dterm_puts(&dt, INV);
                    dterm_putsc(&dt, chp);
                    dterm_puts(&dt, chp);
                }
                break;
                
            case ct_eof:
                dterm_reset(&dt);
                dterm_puts(&dt, "eof\n");
                break;
                
            case ct_error:
                dterm_reset(&dt);
                dterm_puts(&dt, "error\n");
                break;
        }
    }
    
    return dterm_setcan(&dt);
}
*/
 
