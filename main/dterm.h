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

#ifndef dterm_h
#define dterm_h

// Configuration Header
#include "otter_cfg.h"

// Application Headers
#include "cmdhistory.h"
#include "mpipe.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>

//#include <stdlib.h>



#define LINESIZE            1024
#define CMDSIZE             LINESIZE        //CMDSIZE is deprecated
#define READSIZE            3

#define APP_NAME            (OTTER_PARAM(NAME) " " OTTER_PARAM(VERSION))
#define PROMPT_ROOT         OTTER_PARAM(NAME)"# "
#define PROMPT_USER         OTTER_PARAM(NAME)"$ "
#define PROMPT_GUEST        OTTER_PARAM(NAME)"~ "
#define PROMPT              PROMPT_GUEST
#define INV                 PROMPT

#define ASCII_TAB           '\t'
#define ASCII_NEWLN         '\n'
#define ASCII_BACKSPC       '\b'
#define ASCII_DEL           '\x7F'
#define ASCII_BEL           "\a"
#define ASCII_CTLC          '\x03'
#define ASCII_ESC           '\x1B'
#define ASCII_CTLBSLASH     '\x1C'

// for reading from stdout
#define VT100_UPARR         "\x1B[A"
#define VT100_DWARR         "\x1B[B"
#define VT100_RTARR         "\x1B[C"
#define VT100_LFARR         "\x1B[D"

// for writing to stdin
#define VT100_CLEAR_CH      "\b\033[K"
#define VT100_CLEAR_LN      "\033[2K\r"



// describes dterm possible states
typedef enum {
    prompt_off   = 0,
    prompt_on    = 1,
    prompt_close = 2
} prompt_state;


// defines state of dash terminal
typedef struct {
    // old and current terminal settings
    struct termios oldter;
    struct termios curter;
    
    volatile prompt_state state; // state of the terminal prompt
    
    int fd_in;                  // file descriptor for the terminal input
    int fd_out;                 // file descriptor for the terminal output
    
    int linelen;                 // line length
    char *cline;                 // pointer to current position in linebuf
    
    char linebuf[LINESIZE];      // command read buffer
    char readbuf[READSIZE];     // character read buffer
} dterm_t;


typedef struct {
    dterm_t*            dt;
    cmdhist*            ch;
    
    pktlist_t*          tlist;
    
    pthread_mutex_t*    dtwrite_mutex;
    pthread_mutex_t*    tlist_mutex;
    pthread_cond_t*     tlist_cond;
    pthread_mutex_t*    kill_mutex;
    pthread_cond_t*     kill_cond;
} dterm_arg_t;





// describes supported command types
typedef enum {
    ct_sigint       = -3,   // Control-C
    ct_sigquit      = -2,   // Control-\ (backslash)
    ct_error        = -1,   // error reading stdin
    ct_key          = 0,    // stdin eof
    ct_prompt       = 1,
    ct_eof          = 2,
    ct_enter        = 3,    // prompt entered
    ct_autofill     = 4,    // autocomplete query by tab key
    ct_histnext     = 5,    // get next command from history
    ct_histprev     = 6,    // get previous command from history
    ct_delete       = 7,
    ct_ignore       = 8
} cmdtype;



int dterm_open(dterm_t* dt, bool use_pipe);
int dterm_close(dterm_t* dt);
void dterm_free(dterm_t* dt);


// DTerm threads called in main.  
// One one should be started.  
// Piper is for usage with stdin/stdout pipes, via another process.
// Prompter is for usage with user console I/O.
void* dterm_piper(void* args);
void* dterm_prompter(void* args);



// resets command buffer
void dterm_reset(dterm_t *dt);



int dterm_putc(dterm_t *dt, char c);


// writes c string to stdout
// retunrns number of bytes written
int dterm_puts(dterm_t *dt, char *s);


// writes size bytes to stdout
// retunrns number of bytes written
int dterm_put(dterm_t *dt, char *s, int size);


// writes c string to command buffer
// retunrns number of bytes written
int dterm_putsc(dterm_t *dt, char *s);


// printf for dterm.
int dterm_printf(dterm_t* dt, const char* format, ...);

// scanf for dterm.  Scans a line
int dterm_scanf(dterm_t* dt, const char* format, ...);



#endif
