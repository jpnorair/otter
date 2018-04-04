//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "formatters.h"

#include "ppipelist.h"
#include "cliopt.h"
#include "otter_cfg.h"

#include <string.h>
#include <time.h>


// HBuilder is part of Haystack HDO and it is not open source as of 08.2017.
// HBuilder provides a library of DASH7/OpenTag communication API functions 
// that are easy to use.
#if OTTER_FEATURE(HBUILDER)
#   include <hbuilder.h>
#endif


typedef struct {
    uint8_t     id;
    size_t      size;
    uint8_t*    payload;
} alp_list_t;




// Pipe interface input uses hex encoding (deprecated)
#ifdef HEX_OVER_PIPE
static const uint8_t hexlut0[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 16, 32, 48, 64, 80, 96,112,128,144, 0, 0, 0, 0, 0, 0, 
    0,160,176,192,208,224,240,  0,  0,  0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,160,176,192,208,224,240,  0,  0,  0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t hexlut1[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int pipe_gethex(struct pollfd *pubfd, uint8_t* dst, size_t max) {
    int pollcode;
    uint8_t* start;
    uint8_t* end;
    uint8_t byte;
    uint8_t hexbuf[2];
    
    start = dst;
    end = dst + max;
    
    pollcode = poll(pubfd, 1, 100);
    if (pollcode <= 0) {
        goto pipe_gethex_SCRAP;
    }
    else if (pubfd[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        goto pipe_gethex_SCRAP;
    }
    
    while ( (read(pubfd->fd, hexbuf, 2) > 0) && (max--) ) {
        //printf("%c%c (%zu)\n", hexbuf[0], hexbuf[1], max); fflush(stdout);
        byte    = hexlut0[(hexbuf[0]&0x7f)];
        byte   += hexlut1[(hexbuf[1]&0x7f)];
        *dst++  = byte;
    }

    return (int)(dst - start);
    
    pipe_gethex_SCRAP:
    return pollcode;
}
#endif //ifdef HEX_OVER_PIPE







void fmt_printhex(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes, size_t cols) {
    const char convert[] = "0123456789ABCDEF";
    size_t i;
    
    if (cols < 1)
        cols = 1;
    
    i = cols;
    
    while (src_bytes-- != 0) {
        char hexstr[4] = {0, 0, 0, 0};
        
        hexstr[0] = convert[*src >> 4];
        hexstr[1] = convert[*src & 0x0f];
        hexstr[2] = ' ';
        src++;
        puts_fn(hexstr);
        i--;
        
        if ((i == 0) || (src_bytes == 0)) {
            i = cols;
            puts_fn("\n");
        }
        
    }
}



char* _loggermsg_findbreak(char* msg, size_t limit) {
    char* pos;
    long  span;
    
    pos     = strchr(msg, 0);
    span    = (pos-msg);
    
    if ((pos == NULL) || (span >= (long)limit)) {
        pos     = strchr(msg, ' ');
        span    = (pos-msg);
    }
    
    if ((pos == NULL) || (span >= (long)limit)) {
        return NULL;
    }
    
    return pos;
}



void _output_hexlog(mpipe_printer_t puts_fn, uint8_t* payload, int length) {
    uint8_t* msgbreak;

    msgbreak = (uint8_t*)_loggermsg_findbreak((char*)payload, (size_t)length);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - payload);
        
        if (ppipelist_puthex("./pipes/log", (const char*)payload, (char*)msgbreak, length) != 0) {
            puts_fn((char*)payload);
            puts_fn("\n");
            fmt_printhex(puts_fn, msgbreak, length, 16);
        }
    }
}


void _output_binarylog(mpipe_printer_t puts_fn, uint8_t* payload, int length) {
    uint8_t* msgbreak;

    msgbreak = (uint8_t*)_loggermsg_findbreak((char*)payload, (size_t)length);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - payload);
        
        if (ppipelist_putbinary("./pipes/log", (const char*)payload, msgbreak, length) != 0) {
            puts_fn((char*)payload);
            puts_fn("\n");
            fmt_printhex(puts_fn, msgbreak, length, 16);
        }
    }
}

void _output_textlog(mpipe_printer_t puts_fn, uint8_t* payload, int length) {
    uint8_t* msgbreak;

    msgbreak = (uint8_t*)_loggermsg_findbreak((char*)payload, (size_t)length);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - payload);
        
        if (ppipelist_puttext("./pipes/log", (const char*)payload, (char*)msgbreak, length) != 0) {
            puts_fn((char*)payload);
            puts_fn("\n");
            puts_fn((char*)msgbreak);
        }
    }
}


///@todo bypass ALPs that don't have Message-End bit set, save them for later.
///      Will require building an application buffer and maintaining it for an
///      ALP ID until Message End or next Message Start is found.
void fmt_fprintalp(mpipe_printer_t puts_fn, cJSON* msgcall, uint8_t* src, size_t src_bytes) {
    uint8_t* payload;
    int flags;
    int length;
    int rem_bytes;
    int cmd;
    int id;

    /// Early exit conditions
    if ((src_bytes < 4) || (src_bytes > 65535)) {
        return;
    }
    
    /// Null Terminate the end
    /// @note this is safe due to the way src buffer is allocated
    src[src_bytes] = 0;
    
    //fprintf(stderr, "src_bytes=%zu\n", src_bytes);
    rem_bytes = (int)src_bytes;

    do {
        /// Look at ALP header for this record
        ///@todo Buffer it if no Message-End.  Discard Buffer on Message-Start.
        ///      Command value on last record (one with Message-End) will be 
        ///      the Command that is used.
        flags   = src[0];
        length  = src[1];
        id      = src[2];
        cmd     = src[3];
        
        payload     = &src[4];
        rem_bytes  -= 4;
        
        //fprintf(stderr, "flags=%02x length=%02x cmd=%02x id=%02x\n", flags, length, cmd, id);
        
        ///@note could squelch output for mismatched length
        if (length > rem_bytes) {
            length = (int)rem_bytes;
        }
        
        ///@todo deal with multiframe ALPs
        ///      Strategy here is to:
        ///      - only output when Message-End flag is set
        ///      - buffer message until Message-End is set
        ///      - wipe buffer (and start again) whenever Message-Start is set
        ///
        ///      Also, make sure that "length" variable becomes length of
        ///      whole message payload, not just the fragment.
        ///
        
        /// Send the ALP data to an appropriate output pipe.
        /// If output pipe for this ALP isn't defined, nothing will happen.
        /// The id/cmd bytes are sent as well
        {   char str_alpid[8];
            snprintf(str_alpid, 7, "%d", id);
            ppipelist_puthex("./pipes/alp", str_alpid, (char*)&src[2], length+2);
        }
        
        /// If length is 0, print out the ALP header only
        if (length == 0) {
            fmt_printhex(puts_fn, src, 4, 16);
            return;
        }
        
        ///Logger (id 0x04) has special treatment (it gets logged to stdout)
        if (id == 0x04) {
            switch (cmd) {
                // "Raw" Data.  Print as hex
                case 0x00:
                    fmt_printhex(puts_fn, payload, length, 16);
                    break;
            
                // UTF-8 Unstructured Data
                case 0x01:
                    puts_fn((char*)payload);
                    break;
                        
                // Unicode (UTF-16) unstructured Data
                // Not presently supported
                case 0x02:
                    fmt_printhex(puts_fn, src, length+4, 16);
                    break;
                
                // UTF-8 Hex-encoded data
                ///@todo have this print out hex in similar output to fmt_printhex
                case 0x03:
                    puts_fn((char*)payload);
                    break;
                
                // Message with raw data
                case 0x04: {
                    _output_hexlog(puts_fn, payload, length);
                } break;
                        
                // Message with UTF-8 data
                case 0x05:
                    _output_textlog(puts_fn, payload, length);
                    break;
                
                // Message with Unicode (UTF-16) data
                // Not presently supported
                case 0x06:
                    _output_hexlog(puts_fn, payload, length);
                    break;
                    
                // Message with UTF-8 encoded Hex data
                ///@todo have this print out hex in similar output to fmt_printhex
                case 0x07:
                    _output_textlog(puts_fn, payload, length);
                    break;
                
                default:
                    logger_HEXOUT:
                    fmt_printhex(puts_fn, payload, length, 16);
                    break;
            }
        }
        
        /// Punt non supported ALPs to HBUILDER, if HBUILDER is enabled.
#       if OTTER_FEATURE(HBUILDER)
        else if (id != 0) {
            hbuilder_fmtalp(puts_fn, id, cmd, payload, length);
        }
#       endif
        
        /// Dump hex of non-logger ALPs when in verbose mode.
        /// These ALPs get reported to output pipes in verbose and non-verbose modes.
        else if (cliopt_isverbose()) {
            fmt_printhex(puts_fn, src, length+4, 16);
        }
        
        /// Reduce rem_bytes by length and move src ahead by length+4
        /// This allows multiple ALPs to get bundled into a single packet.
        rem_bytes  -= length;
        src        += length+4;
    } while (rem_bytes > 0);
}












int fmt_fprint_external(mpipe_printer_t puts_fn, const char* msgname, cJSON* msgcall, uint8_t* src, size_t size) {
    FILE *pipe_fp;
    //char readbuf[80];

    if ((msgcall == NULL) && (msgname == NULL)) {
        return -1;
    }
        
    msgcall = cJSON_GetObjectItem(msgcall, msgname);
    if (msgcall == NULL) {
        return -1;
    }
    
    if (cJSON_IsString(msgcall) != cJSON_True) {
        return -1;
    }

    /// Open pipe to the call specified for this message type
    /// @note on mac this can be bidirectional, on linux it will need to be
    /// rewritten with popen() and a separate input pipe.
    pipe_fp = popen(msgcall->valuestring, "w");
    //fcntl(pipe_fp, F_SETFL, O_NONBLOCK);
    
    if (pipe_fp != NULL) {
        //fwrite(src, 1, size, pipe_fp);
        const char convert[] = "0123456789ABCDEF";
        for (; size>0; size--, src++) {
            fputc(convert[*src >> 4], pipe_fp);
            fputc(convert[*src & 0x0f], pipe_fp);
        }
        fprintf(pipe_fp, "\n");
    
        //while(fgets(readbuf, 80, pipe_fp)) {
        //    puts_fn(readbuf);
        //}
        
        pclose(pipe_fp);
        return 0;
    }

    return -1;
}




void fmt_hexdump_raw(char* dst, uint8_t* src, size_t src_bytes) {
    const char convert[] = "0123456789ABCDEF";
    
    //convert to hex
    while (src_bytes-- != 0) {
        *dst++  = convert[(*src >> 4)];
        *dst++  = convert[(*src & 0x0f)];
        *dst++  = ' ';
        src++;
    }
    dst--;              // clip last "space" character
    *dst = 0;           // convert "space" to string terminator
}


char* fmt_hexdump_header(uint8_t* data) {
    static char hexdump_buf[32*3 + 1];
    
    /// @todo header inspection to determine length.
    ///       this entails looking at CONTROL field to see encryption type
    
    fmt_hexdump_raw(hexdump_buf, data, 6);
    
    return hexdump_buf;
}


char* fmt_crc(unsigned int crcqual) {
    static char invalid[] = "invalid";
    static char valid[] = "valid";
    return (crcqual) ? invalid : valid;
}


char* fmt_time(time_t* tstamp) {
    static char time_buf[24];
    
    // convert to time using time.h library functions
    strftime(time_buf, 24, "%T", localtime(tstamp) );
    
    return time_buf;
}




