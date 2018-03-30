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

// Application Headers
#include "cmds.h"
#include "cmdhistory.h"
#include "cmdsearch.h"
#include "dterm.h"
#include "test.h"

// Local Libraries/Headers
#include <bintex.h>
#include "m2def.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <ctype.h>


// Dterm variables
const char prompt_guest[]   = PROMPT_GUEST;
const char prompt_user[]    = PROMPT_USER;
const char prompt_root[]    = PROMPT_ROOT;
const char* prompt_str;





// switches terminal to punctual input mode
// returns 0 if success, -1 - fail
int dterm_setnoncan(dterm_t *dt);


// switches terminal to canonical input mode
// returns 0 if success, -1 - fail
int dterm_setcan(dterm_t *dt);


// reads command from stdin
// returns command type
cmdtype dterm_readcmd(dterm_t *dt);





int dterm_putlinec(dterm_t *dt, char c);


// writes size bytes to command buffer
// retunrns number of bytes written
int dterm_putcmd(dterm_t *dt, char *s, int size);


// removes count characters from linebuf
int dterm_remc(dterm_t *dt, int count);


// reads chunk of bytes from stdin
// retunrns non-negative number if success
int dterm_read(dterm_t *dt);


// clears current line, resets command buffer
// return ignored
void dterm_remln(dterm_t *dt);








/** DTerm Control Functions <BR>
  * ========================================================================<BR>
  */


void dterm_free(dterm_t* dt) {
/// So far, nothing to free
}



int dterm_open(dterm_t* dt, bool use_pipe) {
    int retcode;
    
    if (!use_pipe) {
        retcode = tcgetattr(dt->fd_in, &(dt->oldter));
        if (retcode < 0) {
            fprintf(stderr, "Unable to access active termios settings for fd = %d\n", dt->fd_in);
            return retcode;
        }
        
        retcode = tcgetattr(dt->fd_in, &(dt->curter));
        if (retcode < 0) {
            fprintf(stderr, "Unable to access application termios settings for fd = %d\n", dt->fd_in);
            return retcode;
        }
        
        dt->curter.c_lflag     &= ~(ICANON | ECHO);
        dt->curter.c_cc[VMIN]   = 1;
        dt->curter.c_cc[VTIME]  = 0;
        retcode                 = tcsetattr(dt->fd_in, TCSAFLUSH, &(dt->curter));
    }
    else {
        retcode = 0;
    }
    
    dterm_reset(dt);
    
    return retcode;
}



int dterm_close(dterm_t* dt) {
    int retcode = 0;
    
    ///@todo implement dterm_setcan right here
    //if (fcntl(fd, F_GETFD) != -1) { //|| errno != EBADF;
        retcode = tcsetattr(dt->fd_in, TCSAFLUSH, &(dt->oldter));
    //}
    return retcode;
}









/** DTerm Threads <BR>
  * ========================================================================<BR>
  * <LI> dterm_piper()      : For use with input pipe option </LI>
  * <LI> dterm_prompter()   : For use with console entry (default) </LI>
  *
  * Only one of the threads will run.  Piper is much simpler because it just
  * reads stdin pipe as an atomic line read.  Prompter requires character by
  * character input and analysis, and it enables shell-like features.
  */

void* dterm_piper(void* args) {
/// Thread that:
/// <LI> Listens to stdin via read() pipe </LI>
/// <LI> Processes each LINE and takes action accordingly. </LI>
/// 
    
    uint8_t             protocol_buf[1024];
    char                cmdname[256];
    dterm_t*            dt          = ((dterm_arg_t*)args)->dt;
    pktlist_t*          tlist       = ((dterm_arg_t*)args)->tlist;
    pthread_mutex_t*    tlist_mutex = ((dterm_arg_t*)args)->tlist_mutex;
    pthread_cond_t*     tlist_cond  = ((dterm_arg_t*)args)->tlist_cond;
    int                 linelen     = 0;
    char*               linebuf     = dt->linebuf;
    
    
    // Initial state = off
    dt->state = prompt_off;
    
    // Initial prompt = Guest
    prompt_str = prompt_guest;
    
    /// Get each line from the pipe.
    while (1) {
        int cmdlen;
        const cmdtab_item_t* cmdptr;
    
        if (linelen <= 0) {
            dterm_reset(dt);
            linelen = (int)read(dt->fd_in, dt->linebuf, 1024);
            linebuf = dt->linebuf;
        }
        
        // Burn whitespace ahead of command, then search/get command in list.
        while (isspace(*linebuf)) { linebuf++; linelen--; }
        cmdlen  = cmd_getname(cmdname, linebuf, 256);
        cmdptr  = cmd_search(cmdname);
        
        //fprintf(stderr, "\nlinebuf=%s\nlinelen=%d\ncmdname=%s, len=%d, ptr=%016X\n", linebuf, linelen, cmdname, cmdlen, cmdptr);
        
        ///@todo this is the same block of code used in prompter.  It could be
        ///      consolidated into a subroutine called by both.
        if (cmdptr == NULL) {
            ///@todo build a nicer way to show where the error is,
            ///      possibly by using pi or ci (sign reversing)
            dterm_puts(dt, "--> command not found\n");
            linelen = 0;
        }
        else {
            int bytesout;
            int bytesin = 0;

            linebuf += cmdlen;
            linelen -= cmdlen;
            
            ///@todo final arg is max size of protocol_buf.  It should be changed
            ///      to a non constant.
            bytesout = cmd_run((cmdaction_t)cmdptr->action, dt, protocol_buf, &bytesin, (uint8_t*)linebuf, 1024);
            
            /// bytesin is an input that tells how many bytes have been consumed
            /// from the line.  If bytes remain they get treated as the next
            /// command.
            bytesin += 1;           // eat terminator (null or \n)
            linelen -= bytesin;
            linebuf += bytesin;
            
            //fprintf(stderr, "\noutput\nlinebuf=%s\nlinelen=%d\n", linebuf, linelen);
            
            ///@todo spruce-up the command error reporting, maybe even with
            ///      a cursor showing where the first error was found.
            if (bytesout < 0) {
                dterm_puts(dt, "--> command execution error\n");
            }
            
            // If there are bytes to send to MPipe, do that.
            // If bytesout == 0, there is no error, but also nothing
            // to send to MPipe.
            else if (bytesout > 0) {
                int list_size;
                
                // Test only
                //test_dumpbytes(protocol_buf, bytesout, "TX Packet Add");
                // Test only
                
                pthread_mutex_lock(tlist_mutex);
                list_size = pktlist_add(tlist, true, protocol_buf, bytesout);
                pthread_mutex_unlock(tlist_mutex);
                if (list_size > 0) {
                    pthread_cond_signal(tlist_cond);
                }
            }
        }

    }
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: dterm_piper() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}



void* dterm_prompter(void* args) {
/// Thread that:
/// <LI> Listens to dterm-input via read(). </LI>
/// <LI> Processes each keystroke and takes action accordingly. </LI>
/// <LI> Prints to the output while the prompt is active. </LI>
/// <LI> Sends signal (and the accompanied input) to dterm_parser() when a new
///          input is entered. </LI>
/// 
    
    uint8_t protocol_buf[1024];
    
    static const cmdtype npcodes[32] = {
        ct_ignore,          // 00: NUL
        ct_ignore,          // 01: SOH
        ct_ignore,          // 02: STX
        ct_sigint,          // 03: ETX (Ctl+C)
        ct_ignore,          // 04: EOT
        ct_ignore,          // 05: ENQ
        ct_ignore,          // 06: ACK
        ct_ignore,          // 07: BEL
        ct_ignore,          // 08: BS (backspace)
        ct_autofill,        // 09: TAB
        ct_enter,           // 10: LF
        ct_ignore,          // 11: VT
        ct_ignore,          // 12: FF
        ct_ignore,          // 13: CR
        ct_ignore,          // 14: SO
        ct_ignore,          // 15: SI
        ct_ignore,          // 16: DLE
        ct_ignore,          // 17: DC1
        ct_ignore,          // 18: DC2
        ct_ignore,          // 19: DC3
        ct_ignore,          // 20: DC4
        ct_ignore,          // 21: NAK
        ct_ignore,          // 22: SYN
        ct_ignore,          // 23: ETB
        ct_ignore,          // 24: CAN
        ct_ignore,          // 25: EM
        ct_ignore,          // 26: SUB
        ct_prompt,          // 27: ESC (used to invoke prompt, ignored while prompt is up)
        ct_sigquit,         // 28: FS (Ctl+\)
        ct_ignore,          // 29: GS
        ct_ignore,          // 30: RS
        ct_ignore,          // 31: US
    };
    
    cmdtype             cmd;
    char                cmdname[256];
    char                c           = 0;
    ssize_t             keychars    = 0;
    dterm_t*            dt          = ((dterm_arg_t*)args)->dt;
    cmdhist*            ch          = ((dterm_arg_t*)args)->ch;
    pktlist_t*          tlist       = ((dterm_arg_t*)args)->tlist;
    pthread_mutex_t*    write_mutex = ((dterm_arg_t*)args)->dtwrite_mutex;
    pthread_mutex_t*    tlist_mutex = ((dterm_arg_t*)args)->tlist_mutex;
    pthread_cond_t*     tlist_cond  = ((dterm_arg_t*)args)->tlist_cond;
    
    
    // Initial state = off
    dt->state = prompt_off;
    
    // Initial prompt = Guest
    prompt_str = prompt_guest;
    
    /// Get each keystroke.
    /// A keystoke is reported either as a single character or as three.
    /// triple-char keystrokes are for special keys like arrows and control
    /// sequences.
    ///@note dterm_read() will keep the thread asleep, blocking it until data arrives
    while ((keychars = read(dt->fd_in, dt->readbuf, READSIZE)) > 0) {
        
        // Default: IGNORE
        cmd = ct_ignore;
        
        // If dterm state is off, ignore anything except ESCAPE
        ///@todo mutex unlocking on dt->state
        
        if ((dt->state == prompt_off) && (keychars == 1) && (dt->readbuf[0] <= 0x1f)) {
            cmd = npcodes[dt->readbuf[0]];
            
            // Only valid commands when prompt is OFF are prompt, sigint, sigquit
            // Using prompt (ESC) will open a prompt and ignore the escape
            // Using sigquit (Ctl+\) or sigint (Ctl+C) will kill the program
            // Using any other key will be ignored
            if ((cmd != ct_prompt) && (cmd != ct_sigquit) && (cmd != ct_sigint)) {
                continue;
            }
        }
        
        else if (dt->state == prompt_on) {
            if (keychars == 1) {
                c = dt->readbuf[0];   
                if (c <= 0x1F)              cmd = npcodes[c];   // Non-printable characters except DELETE
                else if (c == ASCII_DEL)    cmd = ct_delete;    // Delete (0x7F)
                else                        cmd = ct_key;       // Printable characters
            }
            
            else if (keychars == 3) {
                if ((dt->readbuf[0] == VT100_UPARR[0]) && (dt->readbuf[1] == VT100_UPARR[1])) {
                    if (dt->readbuf[2] == VT100_UPARR[2]) {
                        cmd = ct_histnext;
                    }
                    else if (dt->readbuf[2] == VT100_DWARR[2]) {
                        cmd = ct_histprev;
                    }
                }
            }
        }
        
        // Ignore the keystroke, the prompt is off and/or it is an invalid key
        else {
            continue;
        }
        
        // This mutex protects the terminal output from being written-to by
        // this thread and mpipe_parser() at the same time.
        if (dt->state == prompt_off) {
            pthread_mutex_lock(write_mutex);
        }
        
        // These are error conditions
        if ((int)cmd < 0) {
            int sigcode;
            const char* killstring;
            static const char str_ct_error[]    = "--> terminal read error, sending SIGQUIT\n";
            static const char str_ct_sigint[]   = "^C\n";
            static const char str_ct_sigquit[]  = "^\\\n";
            static const char str_unknown[]     = "--> unknown error, sending SIGQUIT\n";
            
            switch (cmd) {
                case ct_error:      killstring  = str_ct_error;
                                    sigcode     = SIGQUIT;
                                    break;
                                    
                case ct_sigint:     killstring  = str_ct_sigint;
                                    sigcode     = SIGINT; 
                                    break;
                                    
                case ct_sigquit:    killstring  = str_ct_sigquit;
                                    sigcode     = SIGQUIT;
                                    break;
                                    
                default:            killstring  = str_unknown;
                                    sigcode     = SIGQUIT; 
                                    break;
            }
            
            dterm_reset(dt);
            dterm_puts(dt, (char*)killstring);
            raise(sigcode);
            return NULL;
        }
        
        // These are commands that cause input into the prompt.
        // Note that the mutex is only released after ENTER is used, which has
        // the effect of blocking printout of received messages while the 
        // prompt is up
        else {
            int cmdlen;
            char* cmdstr;
            const cmdtab_item_t* cmdptr;
            cmdaction_t cmdfn;
            
            switch (cmd) {
                // A printable key is used
                case ct_key:        dterm_putcmd(dt, &c, 1);
                                    //dterm_put(dt, &c, 1);
                                    dterm_putc(dt, c);
                                    break;
                                    
                // Prompt-Escape is pressed, 
                case ct_prompt:     if (dt->state == prompt_on) {
                                        dterm_remln(dt);
                                        dt->state = prompt_off;
                                    }
                                    else {
                                        dterm_puts(dt, (char*)prompt_str);
                                        dt->state = prompt_on;
                                    }
                                    break;
            
                // EOF currently has the same effect as ENTER/RETURN
                case ct_eof:        
                
                // Enter/Return is pressed
                // 1. Echo Newline (NOTE: not sure why 2 chars here)
                // 2. Add line-entry into the  history
                // 3. Search and try to execute cmd
                // 4. Reset prompt, change to OFF State, unlock mutex on dterm
                case ct_enter:      //dterm_put(dt, (char[]){ASCII_NEWLN}, 2);
                                    dterm_putc(dt, '\n');

                                    if (*(dt->linebuf) && !ch_contains(ch, dt->linebuf)) {
                                        ch_add(ch, dt->linebuf);
                                    }
                                    
                                    cmdlen = cmd_getname(cmdname, dt->linebuf, 256);
                                    cmdptr = cmd_search(cmdname);
                                    if (cmdptr == NULL) {
                                        ///@todo build a nicer way to show where the error is,
                                        ///      possibly by using pi or ci (sign reversing)
                                        dterm_puts(dt, "--> command not found\n");
                                    }
                                    else {
                                        int outbytes;
                                        int inbytes;
                                        uint8_t* cursor = (uint8_t*)&dt->linebuf[cmdlen];
                                        
                                        ///@todo change 1024 to a configured value
                                        outbytes = cmd_run((cmdaction_t)cmdptr->action , dt, protocol_buf, &inbytes, cursor, 1024);
                                        
                                        // Error, print-out protocol_buf as an error message
                                        ///@todo spruce-up the command error reporting, maybe even with
                                        ///      a cursor showing where the first error was found.
                                        if (outbytes < 0) {
                                            dterm_puts(dt, "--> command execution error\n");
                                        }
                                        
                                        // If there are bytes to send to MPipe, do that.
                                        // If rawbytes == 0, there is no error, but also nothing
                                        // to send to MPipe.
                                        else if (outbytes > 0) {
                                            int list_size;
                                            //fprintf(stderr, "packet added to tlist, size = %d bytes\n", outbytes);
                                            pthread_mutex_lock(tlist_mutex);
                                            list_size = pktlist_add(tlist, true, protocol_buf, outbytes);
                                            pthread_mutex_unlock(tlist_mutex);
                                            if (list_size > 0) {
                                                pthread_cond_signal(tlist_cond);
                                            }
                                        }
                                    }
                                    
                                    dterm_reset(dt);
                                    dt->state = prompt_close;
                                    break;
                
                // TAB presses cause the autofill operation (a common feature)
                // autofill will try to finish the command input
                case ct_autofill:   cmdlen = cmd_getname((char*)cmdname, dt->linebuf, 256);
                                    cmdptr = cmd_subsearch((char*)cmdname);
                                    if ((cmdptr != NULL) && (dt->linebuf[cmdlen] == 0)) {
                                        dterm_remln(dt);
                                        dterm_puts(dt, (char*)prompt_str);
                                        dterm_putsc(dt, (char*)cmdptr->name);
                                        dterm_puts(dt, (char*)cmdptr->name);
                                    }
                                    else {
                                        dterm_puts(dt, ASCII_BEL);
                                    }
                                    break;
                
                // DOWN-ARROW presses fill the prompt with the next command 
                // entry in the command history
                case ct_histnext:   cmdstr = ch_next(ch);
                                    if (ch->count && cmdstr) {
                                        dterm_remln(dt);
                                        dterm_puts(dt, (char*)prompt_str);
                                        dterm_putsc(dt, cmdstr);
                                        dterm_puts(dt, cmdstr);
                                    }
                                    break;
                
                // UP-ARROW presses fill the prompt with the last command
                // entry in the command history
                case ct_histprev:   cmdstr = ch_prev(ch);
                                    if (ch->count && cmdstr) {
                                        dterm_remln(dt);
                                        dterm_puts(dt, (char*)prompt_str);
                                        dterm_putsc(dt, cmdstr);
                                        dterm_puts(dt, cmdstr);
                                    }
                                    break;
                
                // DELETE presses issue a forward-DELETE
                case ct_delete:     if (dt->linelen > 0) {
                                        dterm_remc(dt, 1);
                                        dterm_put(dt, VT100_CLEAR_CH, 4);
                                    }
                                    break;
                
                // Every other command is ignored here.
                default:            dt->state = prompt_close;
                                    break;
            }
        }
        
        // Unlock Mutex
        if (dt->state != prompt_on) {
            dt->state = prompt_off;
            pthread_mutex_unlock(write_mutex);
        }
        
    }
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: dterm_prompter() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}










/** Subroutines for reading & writing
  * ========================================================================<BR>
  */


int dterm_read(dterm_t *dt) {
    return (int)read(dt->fd_in, dt->readbuf, READSIZE);
}



int dterm_scanf(dterm_t* dt, const char* format, ...) {
    int retval;
    size_t keychars;
    va_list args;
    
    /// Clear linebuf without actually clearing the line, which is probably a
    /// prompt or similar.  linebuf will hold the rest of the line only
    dterm_reset(dt);
    
    /// Read in a line.  All non-printable characters are ignored.
    while ((keychars = read(dt->fd_in, dt->readbuf, READSIZE)) > 0) {
        if (keychars == 1) {
            if (dt->readbuf[0] == ASCII_NEWLN) {
                dterm_putc(dt, ASCII_NEWLN);
                dterm_putlinec(dt, 0);
            }
            else if ((dt->linelen < LINESIZE) \
            && (dt->readbuf[0] > 0x1f) \
            && (dt->readbuf[0] < 0x7f)) {
                dterm_putc(dt, dt->readbuf[0]);
                dterm_putlinec(dt, dt->readbuf[0]);
            }
        }
    }
    
    /// Run the line through scanf, wrapping the variadic args
    va_start(args, format);
    retval = vsscanf(dt->linebuf, format, args);
    va_end(args);
    
    return retval;
}




int dterm_printf(dterm_t* dt, const char* format, ...) {
    FILE* fp;
    int retval;
    va_list args;
    
    fp = fdopen(dt->fd_out, "w");   //don't close this!  Merely fd --> fp conversion
    if (fp == NULL) {
        return -1;
    }
    
    va_start(args, format);
    retval = vfprintf(fp, format, args);
    va_end(args);
    
    return retval;
}




int dterm_put(dterm_t *dt, char *s, int size) {
    return (int)write(dt->fd_out, s, size);    
}

int dterm_puts(dterm_t *dt, char *s) {
    char* end = s-1;
    while (*(++end) != 0);
        
    return (int)write(dt->fd_out, s, end-s);
}

int dterm_putc(dterm_t *dt, char c) {        
    return (int)write(dt->fd_out, &c, 1);
}

int dterm_puts2(dterm_t *dt, char *s) {
    return (int)write(dt->fd_out, s, strlen(s));
}

int dterm_putsc(dterm_t *dt, char *s) {
    uint8_t* end = (uint8_t*)s - 1;
    while (*(++end) != 0);
    
    return dterm_putcmd(dt, s, end - (uint8_t*)s);
}



int dterm_putlinec(dterm_t *dt, char c) {
    int line_delta = 0;
    
    if (c == ASCII_BACKSPC) {
        line_delta = -1;
    }
    
    else if (c == ASCII_DEL) {
        size_t line_remnant;
        line_remnant = dt->linelen - 1 - (dt->cline - dt->linebuf);
        
        if (line_remnant > 0) {
            memcpy(dt->cline, dt->cline+1, line_remnant);
            line_delta = -1;
        }
    }
    
    else if (dt->linelen > (LINESIZE-1) ) {
        return 0;
    }
    
    else {
        *dt->cline++    = c;
        line_delta      = 1;
    }
    
    dt->linelen += line_delta;
    return line_delta;
}



int dterm_putcmd(dterm_t *dt, char *s, int size) {
    int i;
    
    if ((dt->linelen + size) > LINESIZE) {
        return 0;
    }
        
    dt->linelen += size;
    
    for (i=0; i<size; i++) {
        *dt->cline++ = *s++;
    }
        
    return size;
}




int dterm_remc(dterm_t *dt, int count) {
    int cl = dt->linelen;
    while (count-- > 0) {
        *dt->cline-- = 0;
        dt->linelen--;
    }
    return cl - dt->linelen;
}



void dterm_remln(dterm_t *dt) {
    dterm_put(dt, VT100_CLEAR_LN, 5);
    dterm_reset(dt);
}



void dterm_reset(dterm_t *dt) {
    dt->cline = dt->linebuf;
    
    //int i = LINESIZE;
    //while (--i >= 0) {                            ///@todo this way is the preferred way
    while (dt->cline < (dt->linebuf + LINESIZE)) {
        *dt->cline++ = 0;  
    }
    
    dt->cline    = dt->linebuf;
    dt->linelen  = 0;
}



