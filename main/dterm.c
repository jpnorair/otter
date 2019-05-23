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

///@todo harmonize this with the more slick dterm from otdb project

// Application Headers
#include "cliopt.h"         // to be part of dterm via environment variables
#include "cmdhistory.h"     // to be part of dterm
#include "cmd_api.h"        // to be part of dterm
#include "dterm.h"
#include "otter_app.h"      // must be external to dterm
#include "../test/test.h"
#include "user.h"


// Local Libraries/Headers
#include <argtable3.h>
#include <bintex.h>
#include <cJSON.h>
#include <clithread.h>
#include <cmdtab.h>
#include <m2def.h>

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

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <ctype.h>


#if 0 //OTTER_FEATURE_DEBUG
#   define PRINTLINE()     fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__)
#   define DEBUGPRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#   define PRINTLINE()     do { } while(0)
#   define DEBUGPRINT(...) do { } while(0)
#endif

#ifndef VERBOSE_PRINTF
#   define VERBOSE_PRINTF(...)  do { } while(0)
#endif
#ifndef DEBUG_PRINTF
#   define DEBUG_PRINTF(...)  do { } while(0)
#endif
#ifndef VCLIENT_PRINTF
#   define VCLIENT_PRINTF(...)  do { } while(0)
#endif


// Dterm variables
static const char prompt_guest[]    = PROMPT_GUEST;
static const char prompt_user[]     = PROMPT_USER;
static const char prompt_root[]     = PROMPT_ROOT;
static const char* prompt_str[]     = {
    prompt_root,
    prompt_user,
    prompt_guest
};




// ----------------------------------------------------------------------------
/// Legacy terminal operation subroutines (still used internally)

static int sub_put(dterm_fd_t* fd, char *s, int size);
static int sub_puts(dterm_fd_t* fd, char *s);
static int sub_putc(dterm_fd_t* fd, char c);
static int sub_putcmd(dterm_intf_t *dt, char *s, int size);

// removes count characters from linebuf
static int sub_remc(dterm_intf_t *dt, int count);

static void sub_remln(dterm_intf_t *dt, dterm_fd_t* fd);
static void sub_reset(dterm_intf_t *dt);



static int sub_put(dterm_fd_t* fd, char *s, int size) {
    return (int)write(fd->out, s, size);
}

static int sub_puts(dterm_fd_t* fd, char *s) {
    char* end = s-1;
    while (*(++end) != 0);
    return (int)write(fd->out, s, end-s);
}

static int sub_putc(dterm_fd_t* fd, char c) {
    return (int)write(fd->out, &c, 1);
}

static int sub_putsc(dterm_intf_t *dt, char *s) {
    uint8_t* end = (uint8_t*)s - 1;
    while (*(++end) != 0);
    
    return sub_putcmd(dt, s, (int)(end-(uint8_t*)s) );
}

static int sub_putcmd(dterm_intf_t *dt, char *s, int size) {
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

static int sub_remc(dterm_intf_t *dt, int count) {
    int cl = dt->linelen;
    while (--count >= 0) {
        dt->cline[0] = 0;
        dt->cline--;
        dt->linelen--;
    }
    return cl - dt->linelen;
}

// clears current line, resets command buffer
// return ignored
static void sub_remln(dterm_intf_t *dt, dterm_fd_t* fd) {
    sub_put(fd, VT100_CLEAR_LN, 5);
    sub_reset(dt);
}

static void sub_reset(dterm_intf_t *dt) {
    memset(dt->linebuf, 0, LINESIZE);
    dt->cline   = dt->linebuf;
    dt->linelen = 0;
}

// END OF LEGACY SUBS ---------------------------------------------------------




// DTerm threads called in main.
// Only one should be started.
// Piper is for usage with stdin/stdout pipes, via another process.
// Prompter is for usage with user console I/O.
void* dterm_piper(void* args);
void* dterm_prompter(void* args);
void* dterm_socketer(void* args);



static int sub_rxstat(  char* dst, int dstlimit, DFMT_Type dfmt,
                        void* rxdata, size_t rxsize,
                        uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual   );


static void sub_str_sanitize(char* str, size_t max) {
    while ((*str != 0) && (max != 0)) {
        if (*str == '\r') {
            *str = '\n';
        }
        str++;
        max--;
    }
}

static size_t sub_str_mark(char* str, size_t max) {
    char* s1 = str;
    while ((*str!=0) && (*str!='\n') && (max!=0)) {
        max--;
        str++;
    }
    if (*str=='\n') *str = 0;
    
    return (str - s1);
}

static int sub_hexwrite(int fd, const uint8_t byte) {
    static const char convert[] = "0123456789ABCDEF";
    char dst[2];
    dst[0]  = convert[byte >> 4];
    dst[1]  = convert[byte & 0x0f];
    write(fd, dst, 2);
    return 2;
}

static int sub_hexswrite(char* dst, const uint8_t byte) {
    static const char convert[] = "0123456789ABCDEF";
    *dst++ = convert[byte >> 4];
    *dst++ = convert[byte & 0x0f];
    return 2;
}

static int sub_hexstream(int fd, const uint8_t* src, size_t src_bytes) {
    int bytesout = 0;
    
    while (src_bytes != 0) {
        src_bytes--;
        bytesout += sub_hexwrite(fd, *src++);
    }
    
    return bytesout;
}

static int sub_hexsnstream(char* dst, size_t lim, uint8_t* src, size_t srcsz) {
    char* start = dst;
    lim >>= 1;
    
    while ((srcsz--) && (lim--)) {
        dst += sub_hexswrite(dst, *src++);
    }
    return (int)(dst - start);
}


static void iso_free(void* ctx) {
    talloc_free(ctx);
}

static TALLOC_CTX* iso_ctx;
static void* iso_malloc(size_t size) {
    return talloc_size(iso_ctx, size);
}

static void cjson_iso_allocators(void) {
    cJSON_Hooks hooks;
    hooks.free_fn   = &iso_free;
    hooks.malloc_fn = &iso_malloc;
    cJSON_InitHooks(&hooks);
}

static void cjson_std_allocators(void) {
    cJSON_InitHooks(NULL);
}










/** DTerm Control Functions <BR>
  * ========================================================================<BR>
  */

int dterm_init(dterm_handle_t* dth, void* ext_data, INTF_Type intf) {
    int rc = 0;

    ///@todo ext data should be handled as its own module, but we can accept
    /// that it must be non-null.
    if ((dth == NULL) || (ext_data == NULL)) {
        return -1;
    }
    
    dth->intf = NULL;
    dth->iso_mutex = NULL;
    
    talloc_disable_null_tracking();
    dth->pctx = talloc_new(NULL);
    dth->tctx = NULL;
    if (dth->pctx == NULL){
        rc = -2;
        goto dterm_init_TERM;
    }
    
    dth->ext    = ext_data;
    dth->ch     = NULL;
    dth->intf   = calloc(1, sizeof(dterm_intf_t));
    if (dth->intf == NULL) {
        rc = -3;
        goto dterm_init_TERM;
    }
    
    dth->intf->state = prompt_off;
    dth->intf->type = intf;
    if (intf == INTF_interactive) {
        dth->ch = ch_init(0);
        if (dth->ch == NULL) {
            rc = -4;
            goto dterm_init_TERM;
        }
    }
    
    if (clithread_init(&dth->clithread) != 0) {
        rc = -5;
        goto dterm_init_TERM;
    }
    
    dth->iso_mutex = malloc(sizeof(pthread_mutex_t));
    if (dth->iso_mutex == NULL) {
        rc = -6;
        goto dterm_init_TERM;
    }
    if (pthread_mutex_init(dth->iso_mutex, NULL) != 0 ) {
        rc = -7;
        goto dterm_init_TERM;
    }
    
    return 0;
    
    dterm_init_TERM:
    clithread_deinit(dth->clithread);
    talloc_free(dth->tctx);
    talloc_free(dth->pctx);
    free(dth->iso_mutex);
    free(dth->intf);
    
    return rc;
}



void dterm_deinit(dterm_handle_t* dth) {
    if (dth->intf != NULL) {
        dterm_close(dth);
        free(dth->intf);
    }
    if (dth->ch != NULL) {
        ch_free(dth->ch);
    }
    
    clithread_deinit(dth->clithread);

    if (dth->iso_mutex != NULL) {
        pthread_mutex_unlock(dth->iso_mutex);
        pthread_mutex_destroy(dth->iso_mutex);
        free(dth->iso_mutex);
    }
    
    talloc_free(dth->tctx);
    talloc_free(dth->pctx);
}



dterm_thread_t dterm_open(dterm_handle_t* dth, const char* path) {
    dterm_thread_t dt_thread = NULL;
    int retcode;
    
    if (dth == NULL)        return NULL;
    if (dth->intf == NULL)  return NULL;
    
    if (dth->intf->type == INTF_interactive) {
        /// Need to modify the stdout/stdin attributes in order to work with
        /// the interactive terminal.  The "oldter" setting saves the original
        /// settings.
        dth->fd.in  = STDIN_FILENO;
        dth->fd.out = STDOUT_FILENO;

        retcode = tcgetattr(dth->fd.in, &(dth->intf->oldter));
        if (retcode < 0) {
            perror(NULL);
            fprintf(stderr, "Unable to access active termios settings for fd = %d\n", dth->fd.in);
            goto dterm_open_END;
        }

        retcode = tcgetattr(dth->fd.in, &(dth->intf->curter));
        if (retcode < 0) {
            perror(NULL);
            fprintf(stderr, "Unable to access application termios settings for fd = %d\n", dth->fd.in);
            goto dterm_open_END;
        }
        
        dth->intf->curter.c_lflag      &= ~(ICANON | ECHO);
        dth->intf->curter.c_cc[VMIN]    = 1;
        dth->intf->curter.c_cc[VTIME]   = 0;
        retcode                         = tcsetattr(dth->fd.in, TCSAFLUSH, &(dth->intf->curter));
        if (retcode == 0) {
            dt_thread = &dterm_prompter;
        }
    }
    
    else if (dth->intf->type == INTF_pipe) {
        /// Uses canonical stdin/stdout pipes, no manipulation necessary
        dth->fd.in  = STDIN_FILENO;
        dth->fd.out = STDOUT_FILENO;
        retcode     = 0;
        dt_thread   = &dterm_piper;
    }
    
    else if ((dth->intf->type == INTF_socket) && (path != NULL)) {
        /// Socket mode opens a listening socket
        ///@todo have listening queue size be dynamic
        struct sockaddr_un addr;
        
        dth->fd.in = socket(AF_UNIX, SOCK_STREAM, 0);
        if (dth->fd.in < 0) {
            perror("Unable to create a server socket\n");
            goto dterm_open_END;
        }
        VERBOSE_PRINTF("Socket created on fd=%i\n", dth->fd.in);
        
        ///@todo make sure this unlinking stage is OK.
        ///      unsure how to unbind the socket when server is finished.
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path)-1);
        unlink(path);
        
        VERBOSE_PRINTF("Binding...\n");
        if (bind(dth->fd.in, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("Unable to bind server socket");
            goto dterm_open_END;
        }
        VERBOSE_PRINTF("Binding Socket fd=%i to %s\n", dth->fd.in, path);
        
        if (listen(dth->fd.in, 5) == -1) {
            perror("Unable to enter listen on server socket");
            goto dterm_open_END;
        }
        VERBOSE_PRINTF("Listening on Socket fd=%i\n", dth->fd.in);
        
        retcode     = 0;
        dt_thread   = &dterm_socketer;
    }
    
    else {
        retcode = -1;
    }
    
    dterm_open_END:
    sub_reset(dth->intf);
    return dt_thread;
}




int dterm_close(dterm_handle_t* dth) {
    int retcode;

    if (dth == NULL)        return -1;
    if (dth->intf == NULL)  return -1;
    
    if (dth->intf->type == INTF_interactive) {
        retcode = tcsetattr(dth->fd.in, TCSAFLUSH, &(dth->intf->oldter));
    }
    else {
        retcode = 0;
    }
    
    return retcode;
}




int dterm_squelch(dterm_handle_t* dth) {
    int fd_out = -1;
    
    if (dth != NULL) {
        fd_out          = dth->fd.out;
        dth->fd.squelch = dth->fd.out;
        dth->fd.out     = -1;
    }

    return fd_out;
}



void dterm_unsquelch(dterm_handle_t* dth) {
    if (dth != NULL) {
        if (dth->fd.out < 0) {
            dth->fd.out     = dth->fd.squelch;
            dth->fd.squelch = -1;
        }
    }
}






int dterm_send_cmdmsg(dterm_handle_t* dth, const char* cmdname, const char* msg) {
    if (dth != NULL) {
        if (dth->fd.out >= 0) {
            return dterm_force_cmdmsg(dth->fd.out, cmdname, msg);
        }
    }
    return -1;
}

int dterm_send_error(dterm_handle_t* dth, const char* cmdname, int errcode, uint32_t sid, const char* desc) {
    if (dth != NULL) {
        if (dth->fd.out >= 0) {
            return dterm_force_error(dth->fd.out, cmdname, errcode, sid, desc);
        }
    }
    return -1;
}

int dterm_send_rxstat(dterm_handle_t* dth, DFMT_Type dfmt, void* rxdata, size_t rxsize, uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual) {
    return dterm_publish_rxstat(dth, dfmt, rxdata, rxsize, rxaddr, sid, tstamp, crcqual);
}


///@todo clithread_publish is not safe when the file descriptor is lost before
///      the response arrives.  Need to implement a way to indicate when a
///      client drops in order to skip write and update clithread-table
int dterm_publish_rxstat(dterm_handle_t* dth, DFMT_Type dfmt, void* rxdata, size_t rxsize, uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual) {
    char output[1024];
    int datasize = 0;

    if (dth != NULL) {
        datasize = sub_rxstat(output, 1024, dfmt, rxdata, rxsize, rxaddr, sid, tstamp, crcqual);
        if (datasize > 0) {
            if (dth->intf->type == INTF_socket) {
//{
//struct timespec cur;
//clock_gettime(CLOCK_REALTIME, &cur);
//fprintf(stderr, _E_MAG "dterm_publish_rxstat() sid=%u [%zu.%zu]\n" _E_NRM, sid, cur.tv_sec, cur.tv_nsec);
//}
                clithread_publish(dth->clithread, sid, (uint8_t*)output, datasize);
            }
            else if (dth->fd.out >= 0) {
                write(dth->fd.out, output, datasize);
            }
        }
    }
    return datasize;
}



static int sub_rxstat(  char* dst, int dstlimit, DFMT_Type dfmt,
                        void* rxdata, size_t rxsize,
                        uint64_t rxaddr, uint32_t sid, time_t tstamp, int crcqual) {
    int bytesout;
    int max = dstlimit;
    
    // exit if parameters are incorrect
    if (dstlimit <= 0) {
        return 0;
    }
    
    // Save one char for \n
    dstlimit--;

    ///@todo getformat and isverbose calls should reference dterm data
    switch (cliopt_getformat()) {
        case FORMAT_Hex: {
            bytesout = sub_hexswrite(dst, (crcqual != 0));
            dst += 2;
        } break;
        
        case FORMAT_Json: {
            bytesout = snprintf(dst, dstlimit,
                                "{\"type\":\"rxstat\", "\
                                "\"data\":{\"sid\":%u, \"addr\":\"%llx\", \"qual\":%i, \"time\":%li, ",
                                sid, rxaddr, crcqual, tstamp);
            dstlimit -= bytesout;
            dst      += bytesout;
            if (dstlimit <= 0) break;

            dstlimit -= rxsize;
            if (dstlimit <= 0) break;
            memcpy(dst, rxdata, rxsize);
            dst      += rxsize;
            bytesout += rxsize;
            
            dstlimit -= 2;
            if (dstlimit <= 0) break;
            dst       = stpcpy(dst, "}}");
            bytesout += 2;
        } break;
        
        case FORMAT_JsonHex: {
            bytesout = snprintf(dst, dstlimit,
                                "{\"type\":\"rxstat\", "\
                                "\"data\":{\"sid\":%u, \"addr\":\"%llx\", \"qual\":%i, \"time\":%li, \"frame\":\"",
                                sid, rxaddr, crcqual, tstamp);
            dstlimit -= bytesout;
            dst      += bytesout;
            if (dstlimit <= 0) break;
            
            if (dfmt == DFMT_Binary) {
                int a = sub_hexsnstream(dst, dstlimit, rxdata, rxsize);
                bytesout += a;
                dstlimit -= a;
                dst      += a;
                if (dstlimit <= 0) break;
            }
            else {
                dstlimit -= rxsize;
                if (dstlimit <= 0) break;
                memcpy(dst, rxdata, rxsize);
                dst      += rxsize;
                bytesout += rxsize;
            }
            
            dstlimit -= 3;
            if (dstlimit <= 0) break;
            dst       = stpcpy(dst, "\"}}");
            bytesout += 3;
        } break;
        
        case FORMAT_Bintex: {
            ///@todo this
            bytesout = 0;
        } break;
        
        default: {
            if (cliopt_isverbose()) {
                bytesout = snprintf(dst, dstlimit,
                                    _E_YEL"RX.%u: from %llx at %s, %s"_E_NRM"\n",
                                    sid, rxaddr, fmt_time(&tstamp, NULL), fmt_crc(crcqual, NULL));
            }
            else {
                const char* valid_sym = _E_GRN"v";
                const char* error_sym = _E_RED"x";
                const char* crc_sym   = (crcqual==0) ? valid_sym : error_sym;
                bytesout = snprintf(dst, dstlimit,
                                    _E_WHT"[%u][%llx][%s"_E_WHT"]"_E_NRM" ",
                                    sid, rxaddr, crc_sym);
            }
            dstlimit -= bytesout;
            dst      += bytesout;
            if (dstlimit <= 0) break;
        } break;
    }
    
    if (dstlimit <= 0) {
        dst[bytesout] = '\n';
        bytesout = max;
    }
    else {
        *dst = '\n';
        bytesout++;
    }
    
    return bytesout;
}



///@todo integrate this implementation using fmt_printtext, if possible.
int dterm_force_cmdmsg(int fd_out, const char* cmdname, const char* msg) {
    char output[1024];
    char* dst   = output;
    int lim     = 1024-1;
    int a;

    ///@todo getformat should be stored in dth
    switch (cliopt_getformat()) {
        case FORMAT_Hex: {
            a       = sub_hexswrite(dst, 0);
            dst    += a;
            lim    -= a;
            a       = sub_hexsnstream(dst, lim, (uint8_t*)msg, strlen(msg));
            dst    += a;
            lim    -= a;
        } break;

        case FORMAT_Json:
        case FORMAT_JsonHex: {
            const char* linefront;
            const char* lineback;
            const char* lineend;
            int linesize;
            
            a       = snprintf(dst, lim, "{\"type\":\"msg\", \"data\":{\"cmd\":\"%s\", \"lines\":[", cmdname);
            dst    += a;
            lim    -= a;
            if (lim <= 0) {
                goto dterm_force_cmdmsg_OUT;
            }

            if (msg != NULL) {
                linefront = msg;
                while (*linefront != 0) {
                    // Bypass leading whitespace in the line
                    while (isspace(*linefront)) linefront++;
                    if (*linefront == 0) {
                        break;
                    }
                    
                    // Find next linebreak.
                    // This becomes the array element output line.
                    // If it is the last line, set the eof flag
                    linesize    = (int)strcspn(linefront, "\n");
                    lineend     = &linefront[linesize+1];
                    lineback    = &linefront[linesize];
                    
                    // Trim trailing whitespace from back of the line
                    while (isspace(lineback[-1])) lineback--;
                    linesize = (int)(lineback - linefront);
                    
                    // Deal with quote marks and non-printable characters
                    if (linesize > 0) {
                        *dst++ = '\"';
                        lim--;
                        if (lim <= 0) {
                            goto dterm_force_cmdmsg_OUT;
                        }
                        
                        while ((linefront < lineback) && (lim > 2)) {
                            if ((linefront[0] == '\"') && (linefront[-1] != '\\')) {
                                dst = stpcpy(dst, "\\\"");
                                lim -= 2;
                            }
                            else if ((linefront[0] >= 32) && (linefront[0] <= 126)) {
                                *dst++ = *linefront;
                                lim--;
                            }
                            linefront++;
                        }
                    
                        if ((lineend[0] != 0) && (lim > 3)) {
                            dst = stpcpy(dst, "\", ");
                            lim -= 3;
                        }
                        else {
                            *dst++ = '\"';
                            lim--;
                            if (lim <= 0) break;
                        }
                    }

                    linefront = lineend;
                }
            }
            
            if (lim >= 3) {
                dst = stpcpy(dst, "]}}");
                lim -= 3;
            }
        } break;
        
        case FORMAT_Bintex: ///@todo
            break;
        
        default: {
            a = snprintf(dst, lim, _E_CYN"MSG: "_E_NRM"%s %s", cmdname, msg);
            dst += a;
            lim -= a;
        } break;
    }
    
    dterm_force_cmdmsg_OUT:
    if (lim <= 0) {
        output[1023] = '\n';
        a = 1024;
    }
    else {
        *dst++ = '\n';
        a = (int)(dst - output);
    }
    
    write(fd_out, output, a);
    return a;
}



int dterm_force_error(int fd_out, const char* cmdname, int errcode, uint32_t sid, const char* desc) {
    char output[1024];
    char* dst   = output;
    int lim     = 1024-1;
    int a;
    
    ///@todo getformat should be stored in dth
    switch (cliopt_getformat()) {
        case FORMAT_Hex: {
            a       = sub_hexswrite(dst, (uint8_t)(255 & abs(errcode)));
            dst    += a;
            lim    -= a;
        } break;

        case FORMAT_Json:
        case FORMAT_JsonHex: {
            a       = snprintf(dst, lim, "{\"type\":\"ack\", \"data\":{\"cmd\":\"%s\", \"err\":%i", cmdname, errcode);
            dst    += a;
            lim    -= a;
            if (lim <= 0) {
                break;
            }
            
            a = 0;
            if (errcode == 0) {
                if (sid >= 0) {
                    a = snprintf(dst, lim, ", \"sid\":%u", sid);
                }
            }
            else if (desc != NULL) {
                a = snprintf(dst, lim, ", \"desc\":\"%s\"", desc);
            }
            
            dst    += a;
            lim    -= a;
            if (lim > 0) {
                dst = stpncpy(dst, "}}", lim);
                lim -= 2;
            }
        } break;
        
        case FORMAT_Bintex: ///@todo
            break;
        
        default: {
            if (errcode == 0) {
                a       = snprintf(dst, lim, _E_GRN"ACK: "_E_NRM"%s", cmdname);
                dst    += a;
                lim    -= a;
                if (lim <= 0) {
                    break;
                }
                if (sid != 0) {
                    a       = snprintf(dst, lim, " [%u]", sid);
                    dst    += a;
                    lim    -= a;
                }
            }
            else {
                a       = snprintf(dst, lim, _E_RED"ERR: "_E_NRM"%s (%i)", cmdname, errcode);
                dst    += a;
                lim    -= a;
                if (lim <= 0) {
                    break;
                }
                if (desc != NULL) {
                    a       = snprintf(dst, lim, ": %s", desc);
                    dst    += a;
                    lim    -= a;
                }
            }
        } break;
    }
    
    if (lim <= 0) {
        output[1023] = '\n';
        a = 1024;
    }
    else {
        *dst++ = '\n';
        a = (int)(dst - output);
    }
    
    write(fd_out, output, a);
    return a;
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

static int sub_proc_lineinput(dterm_handle_t* dth, int* cmdrc, char* loadbuf, int linelen) {
    uint8_t     protocol_buf[1024];
    char        cmdname[32];
    int         cmdlen;
    cJSON*      cmdobj;
    uint8_t*    cursor  = protocol_buf;
    int         bufmax  = sizeof(protocol_buf);
    int         bytesout = 0;
    uint32_t    output_sid = 0;
    int         output_err = 0;
    otter_app_t* appdata = dth->ext;
    const cmdtab_item_t* cmdptr;
    
    DEBUG_PRINTF("raw input (%i bytes) %.*s\n", linelen, linelen, loadbuf);

    // Isolation memory context
    iso_ctx = dth->tctx;

    // Set allocators for cJSON, argtable
    cjson_iso_allocators();
    arg_set_allocators(&iso_malloc, &iso_free);
    
    ///@todo set context for other data systems
    
    /// The input can be JSON of the form:
    /// { "type":"${cmd_type}", data:"${cmd_data}" }
    /// where we only truly care about the data object, which must be a string.
    cmdobj = cJSON_Parse(loadbuf);
    if (cJSON_IsObject(cmdobj)) {
        cJSON* dataobj;
        cJSON* typeobj;
        typeobj = cJSON_GetObjectItemCaseSensitive(cmdobj, "type");
        dataobj = cJSON_GetObjectItemCaseSensitive(cmdobj, "data");

        if (cJSON_IsString(typeobj) && cJSON_IsString(dataobj)) {
            int hdr_sz;
            VCLIENT_PRINTF("JSON Request (%i bytes): %.*s\n", linelen, linelen, loadbuf);
            loadbuf = dataobj->valuestring;
            hdr_sz  = snprintf((char*)cursor, bufmax-1, "{\"type\":\"%s\", \"data\":", typeobj->valuestring);
            cursor += hdr_sz;
            bufmax -= hdr_sz;
        }
        else {
            goto sub_proc_lineinput_FREE;
        }
    }
    
    // determine length until newline, or null.
    // then search/get command in list.
    cmdlen  = cmd_getname(cmdname, loadbuf, sizeof(cmdname));
    cmdptr  = cmd_search(appdata->cmdtab, cmdname);
    if (cmdptr == NULL) {
        if (linelen > 0) {
            dterm_send_error(dth, cmdname, 1, 0, "command not found");
        }
    }
    else {
        int bytesin = linelen;
        
        // Null terminate the cursor: errors may report a string.
        *cursor = 0;

        bytesout = cmd_run(cmdptr, dth, cursor, &bytesin, (uint8_t*)(loadbuf+cmdlen), bufmax);

        if (cmdrc != NULL) {
            *cmdrc = bytesout;
        }
        if (bytesout < 0) {
            dterm_send_error(dth, cmdname, bytesout, 0, (char*)cursor);
        }
        else {
            if (bytesout > 0) {
                ///@todo what's between these lines should be a callback provided
                /// by the user of DTerm into DTerm
                // ---------------------------------------------------------------
                // There are bytes to send out via MPipe IO
                ///@todo This "cliopt_isdummy()" call must be changed to a dterm
                ///      state/parameter check.
                if (cliopt_isdummy()) {
                    test_dumpbytes(protocol_buf, bytesout, "TX Packet Add");
                }
                else {
                    pkt_t* txpkt;

                    txpkt = pktlist_add_tx(&appdata->endpoint, NULL, appdata->tlist, protocol_buf, bytesout);
                    if (txpkt != NULL) {
                        output_sid  = txpkt->sequence;
                        pthread_mutex_lock(appdata->tlist_cond_mutex);
                        appdata->tlist_cond_inactive = false;
                        pthread_cond_signal(appdata->tlist_cond);
                        pthread_mutex_unlock(appdata->tlist_cond_mutex);
                    }
                    else {
                        ///@todo come up with a better error code
                        output_err = -32;
                    }
                }
                // ---------------------------------------------------------------
            }
            
            // This is actually an ack message, which is an error with code=0
            // In interactive mode, acks are suppressed
            if (dth->intf->type != INTF_interactive) {
                dterm_send_error(dth, cmdname, output_err, output_sid, NULL);
            }
        }
    }
    
    sub_proc_lineinput_FREE:
    cJSON_Delete(cmdobj);
    
    // Return cJSON and argtable to generic context allocators
    cjson_std_allocators();
    arg_set_allocators(NULL, NULL);
    
    return output_sid;
}



static int sub_readline(size_t* bytesread, int fd, char* buf_a, char* buf_b, int max) {
    size_t bytesin;
    char* start = buf_a;
    int rc = 0;
    char test;
    
    while (max > 0) {
        rc = (int)read(fd, buf_a, 1);
        if (rc != 1) {
            break;
        }
        max--;
        test = *buf_a++;
        if ((test == '\n') || (test == 0)) {
            *buf_a = 0;
            break;
        }
    }
    
    bytesin = (buf_a - start);
    
    if (bytesread != NULL) {
        *bytesread = bytesin;
    }
    
    if (rc >= 0) {
        rc = (int)bytesin;
    }
    
    return rc;
}


void* dterm_socket_clithread(void* args) {
/// Thread that:
/// <LI> Listens to stdin via read() pipe </LI>
/// <LI> Processes each LINE and takes action accordingly. </LI>

    dterm_handle_t* dth;
    dterm_handle_t dts;
    clithread_args_t* ct_args;
    char databuf[1024];
    
    ct_args = (clithread_args_t*)args;
    if (args == NULL)
        return NULL;
    if ((ct_args->app_handle == NULL) || (ct_args->tctx == NULL))
        return NULL;
    
    talloc_disable_null_tracking();

    // Thread-local memory elements
    dth = ((clithread_args_t*)args)->app_handle;
    memcpy(&dts, dth, sizeof(dterm_handle_t));
    dts.fd.in   = ((clithread_args_t*)args)->fd_in;
    dts.fd.out  = ((clithread_args_t*)args)->fd_out;
    dts.tctx    = ct_args->tctx;

    clithread_sigup(ct_args->clithread_self);
    
    // Deferred cancellation: will wait until the blocking read() call is in
    // idle before killing the thread.
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    
    VERBOSE_PRINTF("Client Thread on socket:fd=%i has started\n", dts.fd.out);
    
    /// Get a packet from the Socket
    while (1) {
        int linelen;
        int loadlen;
        char* loadbuf = databuf;
        
        bzero(databuf, sizeof(databuf));

        VERBOSE_PRINTF("Waiting for read on socket:fd=%i\n", dts.fd.out);

        loadlen = sub_readline(NULL, dts.fd.out, loadbuf, NULL, LINESIZE);
        if (loadlen > 0) {
            sub_str_sanitize(loadbuf, (size_t)loadlen);

            pthread_mutex_lock(dts.iso_mutex);
            dts.intf->state = prompt_off;

            do {
                int output_sid;

                // Burn whitespace ahead of command.
                while (isspace(*loadbuf)) { loadbuf++; loadlen--; }
                linelen = (int)sub_str_mark(loadbuf, (size_t)loadlen);

                // Process the line-input command
                output_sid = sub_proc_lineinput(&dts, NULL, loadbuf, linelen);
                clithread_chxid(ct_args->clithread_self, output_sid);

                // +1 eats the terminator
                loadlen -= (linelen + 1);
                loadbuf += (linelen + 1);

            } while (loadlen > 0);

            pthread_mutex_unlock(dts.iso_mutex);
            
        }
        else {
            // After servicing the client socket, it is important to close it.
            close(dts.fd.out);
            break;
        }
    }

    VERBOSE_PRINTF("Client Thread on socket:fd=%i is exiting\n", dts.fd.out);
    
    /// End of thread: it *must* call clithread_exit() before exiting
    clithread_exit( ((clithread_args_t*)args)->clithread_self  );
    return NULL;
}




void* dterm_socketer(void* args) {
/// Thread that:
/// <LI> Listens to stdin via read() pipe </LI>
/// <LI> Processes each LINE and takes action accordingly. </LI>
    dterm_handle_t* dth = (dterm_handle_t*)args;
    clithread_args_t clithread;
    
    // Socket operation has no interface prompt
    dth->intf->state        = prompt_off;
    clithread.app_handle    = dth;
    clithread.fd_in         = dth->fd.in;
    clithread.tctx          = NULL;
    
    /// Get a packet from the Socket
    while (dth->thread_active) {
        VERBOSE_PRINTF("Waiting for client accept on socket fd=%i\n", dth->fd.in);
        clithread.fd_out = accept(dth->fd.in, NULL, NULL);
        if (clithread.fd_out < 0) {
            perror("Server Socket accept() failed");
        }
        else {
            size_t poolsize     = cliopt_getpoolsize();
            size_t est_poolobj  = 4;
            clithread_add(dth->clithread, NULL, est_poolobj, poolsize, &dterm_socket_clithread, (void*)&clithread);
        }
    }
    
    return NULL;
}



void* dterm_piper(void* args) {
/// Thread that:
/// <LI> Listens to stdin via read() pipe </LI>
/// <LI> Processes each LINE and takes action accordingly. </LI>
    dterm_handle_t* dth     = (dterm_handle_t*)args;
    int             loadlen = 0;
    char*           loadbuf = dth->intf->linebuf;
    
    // Initial state = off
    dth->intf->state = prompt_off;
    
    talloc_disable_null_tracking();
    
    /// Get each line from the pipe.
    while (dth->thread_active) {
        int linelen;
        size_t poolsize;
        size_t est_poolobj;
        
        if (loadlen <= 0) {
            sub_reset(dth->intf);
            loadlen = (int)read(dth->fd.in, loadbuf, 1024);
            sub_str_sanitize(loadbuf, (size_t)loadlen);
        }
        
        // Burn whitespace ahead of command.
        while (isspace(*loadbuf)) { loadbuf++; loadlen--; }
        linelen = (int)sub_str_mark(loadbuf, (size_t)loadlen);

        // Create temporary context as a memory pool
        poolsize    = cliopt_getpoolsize();
        est_poolobj = 4; //(poolsize / 128) + 1;
        dth->tctx   = talloc_pooled_object(NULL, void*, est_poolobj, poolsize);

        // Process the line-input command
        sub_proc_lineinput(dth, NULL, loadbuf, linelen);
        
        // Free temporary memory pool context
        talloc_free(dth->tctx);
        dth->tctx = NULL;
        
        // +1 eats the terminator
        loadlen -= (linelen + 1);
        loadbuf += (linelen + 1);
    }
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    //fprintf(stderr, "\n--> Chaotic error: dterm_piper() thread broke loop.\n");
    //raise(SIGINT);
    return NULL;
}



///@note dterm_cmdfile() was copied from the otdb project, which has more
///      sopisticated dterm implementation.  Portions of dterm_cmdfile()
///      have been commented-out and/or modified by a new line below in order
///      to fit with the more primitive otter dterm impl.
int dterm_cmdfile(dterm_handle_t* dth, const char* filename) {
    int     filebuf_sz;
    char*   filecursor;
    char*   filebuf     = NULL;
    int     rc          = 0;
    FILE*   fp          = NULL;
    dterm_fd_t local;
    dterm_fd_t saved;
    
    // Initial state = off
    dth->intf->state = prompt_off;

    // Open the file, Load the contents into filebuf
    fp = fopen(filename, "r");
    if (fp == NULL) {
        //perror(ERRMARK"cmdfile couldn't be opened");
        return -1;
    }

    fseek(fp, 0L, SEEK_END);
    filebuf_sz = (int)ftell(fp);
    rewind(fp);
    filebuf = talloc_zero_size(dth->pctx, filebuf_sz+1);
    
    if (filebuf == NULL) {
        rc = -2;
        goto dterm_cmdfile_END;
    }

    rc = !(fread(filebuf, filebuf_sz, 1, fp) == 1);
    if (rc != 0) {
        //perror(ERRMARK"cmdfile couldn't be read");
        rc = -3;
        goto dterm_cmdfile_END;
    }

    // File stream no longer required
    fclose(fp);
    fp = NULL;

    // Preprocess the command inputs strings
    sub_str_sanitize(filebuf, (size_t)filebuf_sz);

    // Reset the terminal to default state
    sub_reset(dth->intf);

    pthread_mutex_lock(dth->iso_mutex);
    local.in    = STDIN_FILENO;
    local.out   = STDOUT_FILENO;
    saved       = dth->fd;
    dth->fd     = local;

    // Run the command on each line
    filecursor = filebuf;
    while (filebuf_sz > 0) {
        int linelen;
        int cmdrc;
        size_t poolsize;
        size_t est_poolobj;
        
        // Burn whitespace ahead of command.
        while (isspace(*filecursor)) { filecursor++; filebuf_sz--; }
        linelen = (int)sub_str_mark(filecursor, (size_t)filebuf_sz);

        // Create temporary context as a memory pool
        poolsize    = cliopt_getpoolsize();
        est_poolobj = 4; //(poolsize / 128) + 1;
        dth->tctx   = talloc_pooled_object(NULL, void, est_poolobj, poolsize);
        
        // Echo input line to dterm
        dprintf(dth->fd.out, _E_MAG"%s"_E_NRM"%s\n", prompt_root, filecursor);
        
        // Process the line-input command
        sub_proc_lineinput(dth, &cmdrc, filecursor, linelen);
        
        // Free temporary memory pool context
        talloc_free(dth->tctx);
        dth->tctx = NULL;
        
        // Exit the command sequence on first detection of error.
        if (cmdrc < 0) {
            dprintf(dth->fd.out, _E_RED"ERR: "_E_NRM"Command Returned %i: stopping.\n\n", cmdrc);
            break;
        }
        
        // +1 eats the terminator
        filebuf_sz -= (linelen + 1);
        filecursor += (linelen + 1);
    }
    
    dth->fd = saved;
    pthread_mutex_unlock(dth->iso_mutex);

    dterm_cmdfile_END:
    if (fp != NULL) fclose(fp);
    talloc_free(filebuf);
    
    return rc;
}





void* dterm_prompter(void* args) {
/// Thread that:
/// <LI> Listens to dterm-input via read(). </LI>
/// <LI> Processes each keystroke and takes action accordingly. </LI>
/// <LI> Prints to the output while the prompt is active. </LI>
/// <LI> Sends signal (and the accompanied input) to dterm_parser() when a new
///          input is entered. </LI>
///
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
    dterm_handle_t*     dth         = args;
    
    ///@todo this needs to be abstracted to not require cmdtab or endpoint.usertype
    otter_app_t*        appdata     = dth->ext;
    
    
    if (dth->ext != appdata) {
        fprintf(stderr, "Linkage error between dterm handle and application data.\n");
        goto dterm_prompter_TERM;
    }

    talloc_disable_null_tracking();

    // Initial state = off
    dth->intf->state = prompt_off;
    
    /// Get each keystroke.
    /// A keystoke is reported either as a single character or as three.
    /// triple-char keystrokes are for special keys like arrows and control
    /// sequences.
    while ((keychars = read(dth->fd.in, dth->intf->readbuf, READSIZE)) > 0) {
        
        // Default: IGNORE
        cmd = ct_ignore;
        
        // If dterm state is off, ignore anything except ESCAPE
        ///@todo mutex unlocking on dt->state
        
        if ((dth->intf->state == prompt_off) && (keychars == 1) && (dth->intf->readbuf[0] <= 0x1f)) {
            cmd = npcodes[dth->intf->readbuf[0]];
            
            // Only valid commands when prompt is OFF are prompt, sigint, sigquit
            // Using prompt (ESC) will open a prompt and ignore the escape
            // Using sigquit (Ctl+\) or sigint (Ctl+C) will kill the program
            // Using any other key will be ignored
            if ((cmd != ct_prompt) && (cmd != ct_sigquit) && (cmd != ct_sigint)) {
                continue;
            }
        }
        
        else if (dth->intf->state == prompt_on) {
            if (keychars == 1) {
                c = dth->intf->readbuf[0];
                if (c <= 0x1F)              cmd = npcodes[c];   // Non-printable characters except DELETE
                else if (c == ASCII_DEL)    cmd = ct_delete;    // Delete (0x7F)
                else                        cmd = ct_key;       // Printable characters
            }
            
            else if (keychars == 3) {
                if ((dth->intf->readbuf[0] == VT100_UPARR[0]) && (dth->intf->readbuf[1] == VT100_UPARR[1])) {
                    if (dth->intf->readbuf[2] == VT100_UPARR[2]) {
                        cmd = ct_histnext;
                    }
                    else if (dth->intf->readbuf[2] == VT100_DWARR[2]) {
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
        if (dth->intf->state == prompt_off) {
            pthread_mutex_lock(dth->iso_mutex);
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
            
            sub_reset(dth->intf);
            sub_puts(&dth->fd, (char*)killstring);

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
            
            switch (cmd) {
                // A printable key is used
                case ct_key: {
                    sub_putcmd(dth->intf, &c, 1);
                    //sub_putdt, &c, 1);
                    sub_putc(&dth->fd, c);
                } break;
                                    
                // Prompt-Escape is pressed, 
                case ct_prompt: {
                    if (dth->intf->state == prompt_on) {
                        sub_remln(dth->intf, &dth->fd);
                        dth->intf->state = prompt_off;
                    }
                    else {
                        dprintf(dth->fd.out, _E_MAG"%s"_E_NRM, (char*)prompt_str[appdata->endpoint.usertype]);
                        dth->intf->state = prompt_on;
                    }
                } break;
            
                // EOF currently has the same effect as ENTER/RETURN
                case ct_eof:        
                
                // Enter/Return is pressed
                // 1. Echo Newline (NOTE: not sure why 2 chars here)
                // 2. Add line-entry into the  history
                // 3. Search and try to execute cmd
                // 4. Reset prompt, change to OFF State, unlock mutex on dterm
                case ct_enter: {
                    size_t poolsize;
                    size_t est_poolobj;
                    sub_putc(&dth->fd, '\n');

                    if (!ch_contains(dth->ch, dth->intf->linebuf)) {
                        ch_add(dth->ch, dth->intf->linebuf);
                    }

                    // Create temporary context as a memory pool
                    poolsize    = cliopt_getpoolsize();
                    est_poolobj = 4; //(poolsize / 128) + 1;
                    dth->tctx   = talloc_pooled_object(NULL, void*, est_poolobj, poolsize);

                    // Run command(s) from line input
                    sub_proc_lineinput( dth, NULL,
                                        (char*)dth->intf->linebuf,
                                        (int)sub_str_mark((char*)dth->intf->linebuf, 1024)
                                    );

                    // Free temporary memory pool context
                    talloc_free(dth->tctx);
                    dth->tctx = NULL;

                    sub_reset(dth->intf);
                    dth->intf->state = prompt_close;
                } break;
                
                // TAB presses cause the autofill operation (a common feature)
                // autofill will try to finish the command input
                case ct_autofill: {
                    cmdlen = cmd_getname((char*)cmdname, dth->intf->linebuf, 256);
                    cmdptr = cmd_subsearch(appdata->cmdtab, (char*)cmdname);
                    if ((cmdptr != NULL) && (dth->intf->linebuf[cmdlen] == 0)) {
                        sub_remln(dth->intf, &dth->fd);
                        dprintf(dth->fd.out, _E_MAG"%s"_E_NRM, (char*)prompt_str[appdata->endpoint.usertype]);
                        sub_putsc(dth->intf, (char*)cmdptr->name);
                        sub_puts(&dth->fd, (char*)cmdptr->name);
                    }
                    else {
                        sub_puts(&dth->fd, ASCII_BEL);
                    }
                } break;
                
                // DOWN-ARROW presses fill the prompt with the next command 
                // entry in the command history
                case ct_histnext: {
                    cmdstr = ch_next(dth->ch);
                    if (dth->ch->count && cmdstr) {
                        sub_remln(dth->intf, &dth->fd);
                        dprintf(dth->fd.out, _E_MAG"%s"_E_NRM, (char*)prompt_str[appdata->endpoint.usertype]);
                        sub_putsc(dth->intf, cmdstr);
                        sub_puts(&dth->fd, cmdstr);
                    }
                } break;
                
                // UP-ARROW presses fill the prompt with the last command
                // entry in the command history
                case ct_histprev: {
                    cmdstr = ch_prev(dth->ch);
                    if (dth->ch->count && cmdstr) {
                        sub_remln(dth->intf, &dth->fd);
                        dprintf(dth->fd.out, _E_MAG"%s"_E_NRM, (char*)prompt_str[appdata->endpoint.usertype]);
                        sub_putsc(dth->intf, cmdstr);
                        sub_puts(&dth->fd, cmdstr);
                    }
                } break;
                
                // DELETE presses issue a forward-DELETE
                case ct_delete: {
                    if (dth->intf->linelen > 0) {
                        sub_remc(dth->intf, 1);
                        sub_put(&dth->fd, VT100_CLEAR_CH, 4);
                    }
                } break;
                
                // Every other command is ignored here.
                default: {
                    dth->intf->state = prompt_close;
                } break;
            }
        }
        
        // Unlock Mutex
        if (dth->intf->state != prompt_on) {
            dth->intf->state = prompt_off;
            pthread_mutex_unlock(dth->iso_mutex);
        }
        
        if (dth->thread_active == false) {
            break;
        }
    }
    
    dterm_prompter_TERM:
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    //fprintf(stderr, "\n--> Chaotic error: dterm_prompter() thread broke loop.\n");
    //raise(SIGINT);
    return NULL;
}



