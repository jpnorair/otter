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

// Local Libraries/Headers
#include "bintex.h"
#include "m2def.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>







// switches terminal to punctual input mode
// returns 0 if success, -1 - fail
int dterm_setnoncan(dterm_t *dt);


// switches terminal to canonical input mode
// returns 0 if success, -1 - fail
int dterm_setcan(dterm_t *dt);


// reads command from stdin
// returns command type
cmdtype dterm_readcmd(dterm_t *dt);


// writes size bytes to command buffer
// retunrns number of bytes written
int dterm_putcmd(dterm_t *dt, char *s, int size);


// removes count characters from cmdbuf
int dterm_remc(dterm_t *dt, int count);


// reads chunk of bytes from stdin
// retunrns non-negative number if success
int dterm_read(dterm_t *dt);


// resets command buffer
void dterm_reset(dterm_t *dt);


// clears current line, resets command buffer
// return ignored
void dterm_remln(dterm_t *dt);








/** DTerm Control Functions <BR>
  * ========================================================================<BR>
  */


void dterm_free(dterm_t* dt) {
/// So far, nothing to free
}



int dterm_open(dterm_t* dt) {
    int retcode;
    
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
    
    dterm_reset(dt);
    
    return retcode;
}



int dterm_close(dterm_t* dt) {
    int retcode;
    
    ///@todo implement dterm_setcan right here
    retcode = tcsetattr(dt->fd_in, TCSAFLUSH, &(dt->oldter));
    return retcode;
}









/** DTerm Threads <BR>
  * ========================================================================<BR>
  * <LI> dterm_prompter() : </LI>
  */


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
    int                 keychars    = 0;
    dterm_t*            dt          = ((dterm_arg_t*)args)->dt;
    cmdhist*            ch          = ((dterm_arg_t*)args)->ch;
    pthread_mutex_t*    write_mutex = ((dterm_arg_t*)args)->dtwrite_mutex;
    
    // Initial state = off
    dt->state = prompt_off;
    
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
            int pi, ci;
            char* cmdstr;
        
            switch (cmd) {
                // A printable key is used
                case ct_key:        dterm_putcmd(dt, &c, 1);
                                    dterm_put(dt, &c, 1);
                                    break;
                                    
                // Prompt-Escape is pressed, 
                case ct_prompt:     dterm_puts(dt, PROMPT);
                                    dt->state = prompt_on;
                                    break;
            
                // EOF currently has the same effect as ENTER/RETURN
                case ct_eof:        
                
                // Enter/Return is pressed
                // 1. Echo Newline (NOTE: not sure why 2 chars here)
                // 2. Add line-entry into the  history
                // 3. Search and try to execute cmd
                // 4. Reset prompt, change to OFF State, unlock mutex on dterm
                case ct_enter:      dterm_put(dt, (char[]){ASCII_NEWLN}, 2);

                                    if (*(dt->cmdbuf) && !ch_contains(ch, dt->cmdbuf)) {
                                        ch_add(ch, dt->cmdbuf);
                                    }
                                    
                                    pi = cmd_parse(cmdname, dt->cmdbuf);
                                    ci = cmd_search(cmdname);
                                    if ((pi < 0) || (ci < 0)) {
                                        ///@todo build a nicer way to show where the error is,
                                        ///      possibly by using pi or ci (sign reversing)
                                        dterm_puts(dt, "--> command not found\n");
                                    }
                                    else {
                                        int retval;
                                        ///@todo change 1024 to a configured value
                                        retval = commands[ci].method( \
                                                        protocol_buf, 
                                                        (uint8_t*)(dt->cmdbuf + pi),
                                                        1024, 
                                                        CMDSIZE   );
                                        if (retval != 0) {
                                            dterm_puts(dt, "--> command failed\n");
                                        }
                                        else {
                                            dterm_puts(dt, "--> command completed\n");
                                            
                                        }
                                    }
                                    
                                    dterm_reset(dt);
                                    dt->state = prompt_close;
                                    break;
                
                // TAB presses cause the autofill operation (a common feature)
                // autofill will try to finish the command input
                case ct_autofill:   pi = cmd_parse(dt->cmdbuf, cmdname);
                                    ci = cmd_subsearch(cmdname);
                                    if ((pi > -1) && (ci > -1) && (*(dt->cmdbuf + pi) == 0)) {
                                        dterm_remln(dt);
                                        dterm_puts(dt, PROMPT);
                                        dterm_putsc(dt, commands[ci].name);
                                        dterm_puts(dt, commands[ci].name);
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
                                        dterm_puts(dt, PROMPT);
                                        dterm_putsc(dt, cmdstr);
                                        dterm_puts(dt, cmdstr);
                                    }
                                    break;
                
                // UP-ARROW presses fill the prompt with the last command
                // entry in the command history
                case ct_histprev:   cmdstr = ch_prev(ch);
                                    if (ch->count && cmdstr) {
                                        dterm_remln(dt);
                                        dterm_puts(dt, PROMPT);
                                        dterm_putsc(dt, cmdstr);
                                        dterm_puts(dt, cmdstr);
                                    }
                                    break;
                
                // DELETE presses issue a forward-DELETE
                case ct_delete:     if (dt->cmdlen > 0) {
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
/*
cmdtype dterm_readcmd(dterm_t *dt) {
///@todo make this use
    char c = 0;
    int rr = 0;
    
    while ((rr = (int)read(dt->fd_in, dt->readbuf, READSIZE)) > 0) {
        switch (rr) {
            case 1:
                c = dt->readbuf[0];
            
                // enter
                if (c == ASCII_NEWLN) {
                    dterm_put(dt, (char[]){ASCII_NEWLN}, 2);
                    return ct_simple;
                }
            
                // tab
                if (c == ASCII_TAB)
                    return ct_autocomplete;
            
                // delete
                if (c == ASCII_DEL && dt->cmdlen > 0) {
                    dterm_remc(dt, 1);
                    dterm_put(dt, VT100_CLEAR_CH, 4);
                }
            
                // character
                if (c > 0x1F && c < 0x7F) {
                    dterm_put(dt, &c, 1);
                    dterm_putcmd(dt, &c, 1);
                }
                break;
            
            case 3:
        
                // up/down arrows                
                if (dt->readbuf[0] == VT100_UPARR[0] &&
                    dt->readbuf[1] == VT100_UPARR[1]) {
                
                    c = dt->readbuf[2];
                    if (dt->readbuf[2] == VT100_UPARR[2])
                        return ct_histnext;
                    else if (dt->readbuf[2] == VT100_DWARR[2])
                        return ct_histprev;
                }
                break;
        }
    }  
    
    return (rr == 0) ? ct_eof : ct_error;
}
*/


int dterm_read(dterm_t *dt) {
    return (int)read(dt->fd_in, dt->readbuf, READSIZE);
}


int dterm_put(dterm_t *dt, char *s, int size) {
    return (int)write(dt->fd_out, s, size);    
}

int dterm_puts(dterm_t *dt, char *s) {
    char* end = s-1;
    while (*(++end) != 0);
        
    return (int)write(dt->fd_out, s, end-s);
}

int dterm_puts2(dterm_t *dt, char *s) {
    return (int)write(dt->fd_out, s, strlen(s));
}

int dterm_putsc(dterm_t *dt, char *s) {
    uint8_t* end = (uint8_t*)s - 1;
    while (*(++end) != 0);
    
    return dterm_putcmd(dt, s, end - (uint8_t*)s);
}

int dterm_putcmd(dterm_t *dt, char *s, int size) {
    int i;
    
    if ((dt->cmdlen + size) > CMDSIZE) {
        return 0;
    }
        
    dt->cmdlen += size;
    
    for (i=0; i<size; i++) {
        *dt->ccmd++ = *s++;
    }
        
    return size;
}




int dterm_remc(dterm_t *dt, int count) {
    int cl = dt->cmdlen;
    while (count-- > 0) {
        *dt->ccmd-- = 0;
        dt->cmdlen--;
    }
    return cl - dt->cmdlen;
}



void dterm_remln(dterm_t *dt) {
    dterm_put(dt, VT100_CLEAR_LN, 5);
    dterm_reset(dt);
}



void dterm_reset(dterm_t *dt) {
    dt->ccmd = dt->cmdbuf;
    
    //int i = CMDSIZE;
    //while (--i >= 0) {                            ///@todo this way is the preferred way
    while (dt->ccmd < (dt->cmdbuf + CMDSIZE)) {
        *dt->ccmd++ = 0;  
    }
    
    dt->ccmd    = dt->cmdbuf;
    dt->cmdlen  = 0;
}



