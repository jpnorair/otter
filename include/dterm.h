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
#include "cliopt.h"
#include "otter_cfg.h"
#include "pktlist.h"
#include "subscribers.h"
#include "user.h"

// Application Headers
#include "cmdhistory.h"
#include "mpipe.h"

// HB Libraries
#include <clithread.h>
#include <cmdtab.h>
#include <talloc.h>

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

typedef enum {
    DFMT_Binary,
    DFMT_Text,
    DFMT_Native,
    DFMT_Max
} DFMT_Type;

// describes dterm possible states
typedef enum {
    prompt_off   = 0,
    prompt_on    = 1,
    prompt_close = 2
} prompt_state;


// defines state of dash terminal
typedef struct {
    INTF_Type type;                 // Socket, Pipe, Interactive, etc.
    volatile prompt_state state;    // state of the terminal prompt

    // old and current terminal settings
    struct termios oldter;
    struct termios curter;
    
    //int fd_in;                      // file descriptor for the terminal input
    //int fd_out;                     // file descriptor for the terminal output
    //int fd_squelch;                 // For squelch feature
    
    int linelen;                    // line length
    char* cline;                    // pointer to current position in linebuf
    char linebuf[LINESIZE];         // command read buffer
    char readbuf[READSIZE];         // character read buffer
} dterm_intf_t;


typedef struct {
    // fd_in, fd_out are used by controlling interface.
    // Usage will differ in case of interactive, pipe, socket
    int in;
    int out;
    int squelch;
} dterm_fd_t;




typedef struct {
    // Internally initialized in dterm_init()
    // Used only by dterm controlling thread.
    dterm_intf_t*       intf;
    cmdhist*            ch;
    clithread_handle_t  clithread;
    
    // Client Thread I/O parameters.
    // Should be altered per client thread in cloned dterm_handle_t
    dterm_fd_t          fd;
    
    // Process Context:
    // Thread Context: may be null if not using talloc
    TALLOC_CTX*         pctx;
    TALLOC_CTX*         tctx;
    
    // Isolation Mutex
    // * Used by dterm client threads to prevent more than one command from
    //    running at any given time.
    // * Initialized by DTerm
    pthread_mutex_t*    iso_mutex;
    
    // Thread flags
    bool thread_active;
    
    // Externally Initialized data elements
    // These should only be used within lock provided by isolation mutex
    void*               ext;

} dterm_handle_t;



typedef void* (*dterm_thread_t)(void*);


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


///@todo rework the dterm module into a more normal object.
/// Presently the handle isn't a real handle, it's a struct.
int dterm_init(dterm_handle_t* dth, void* ext_data, INTF_Type intf);
void dterm_deinit(dterm_handle_t* handle);

int dterm_cmdfile(dterm_handle_t* dth, const char* filename);


dterm_thread_t dterm_open(dterm_handle_t* dth, const char* path);
int dterm_close(dterm_handle_t* dth);


int dterm_squelch(dterm_handle_t* dt);
void dterm_unsquelch(dterm_handle_t* dt);


int dterm_send_error(dterm_handle_t* dth, const char* cmdname, int errcode, uint32_t sid, const char* desc);
int dterm_send_cmdmsg(dterm_handle_t* dth, const char* cmdname, const char* msg);
int dterm_send_rxstat(dterm_handle_t* dth, DFMT_Type dfmt, void* rxdata, size_t rxsize, uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual);

int dterm_force_error(int fd_out, const char* cmdname, int errcode, uint32_t sid, const char* desc);
int dterm_force_cmdmsg(int fd_out, const char* cmdname, const char* msg);
int dterm_force_rxstat(int fd_out, DFMT_Type dfmt, void* rxdata, size_t rxsize, uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual);


/** @note All of these dterm output functions are deprecated and in danger of
  * being removed from the code base in favor of direct utilization of dterm
  * file descriptors.
  */

int dterm_put(dterm_fd_t* fd, char *s, int size);
int dterm_puts(dterm_fd_t* fd, char *s);
int dterm_putc(dterm_fd_t* fd, char c);
int dterm_puts2(dterm_fd_t* fd, char *s);
//int dterm_put(dterm_intf_t *dt, char *s, int size);
//int dterm_puts(dterm_intf_t *dt, char *s);
//int dterm_putc(dterm_intf_t *dt, char c);
//int dterm_puts2(dterm_fd_t* fd, char *s);


// writes c string to command buffer
// retunrns number of bytes written
int dterm_putsc(dterm_intf_t *dt, char *s);
int dterm_putcmd(dterm_intf_t *dt, char *s, int size);

// resets command buffer
void dterm_reset(dterm_intf_t *dt);


#endif
