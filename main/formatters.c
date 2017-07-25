//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "formatters.h"

#include "ppipelist.h"

#include <string.h>
#include <time.h>






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


void _output_binarylog(mpipe_printer_t puts_fn, uint8_t* payload, int length) {
    uint8_t* msgbreak;

    msgbreak = (uint8_t*)_loggermsg_findbreak((char*)payload, (size_t)length);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - payload);
        
        if (ppipelist_putbinary("log", (const char*)payload, msgbreak, length) != 0) {
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
        
        if (ppipelist_puttext("log", (const char*)payload, (char*)msgbreak, length) != 0) {
            puts_fn((char*)payload);
            puts_fn("\n");
            puts_fn((char*)msgbreak);
        }
    }
}


void fmt_fprintalp(mpipe_printer_t puts_fn, cJSON* msgcall, uint8_t* src, size_t src_bytes) {
    uint8_t* payload;
    int flags;
    int length;
    int cmd;
    int id;

    /// Early exit condition
    if (src_bytes < 4) {
        return;
    }
    
    //fprintf(stderr, "src_bytes=%zu\n", src_bytes);

    /// Null Terminate the end
    /// @note this is safe due to the way src buffer is allocated
    src[src_bytes] = 0;

    /// Look at ALP header
    flags   = src[0];
    length  = src[1];
    id      = src[2];
    cmd     = src[3];
    
    payload     = &src[4];
    src_bytes  -= 4;
    
    //fprintf(stderr, "flags=%02x length=%02x cmd=%02x id=%02x\n", flags, length, cmd, id);
    
    
    ///@note could squelch output for mismatched length
    if (length > src_bytes) {
        length = (int)src_bytes;
    }
    
    /// If length is 0, print out the ALP header only
    if (length == 0) {
        fmt_printhex(puts_fn, src, src_bytes, 16);
        return;
    }
    
    
    ///@todo deal with multiframe ALPs
    
    ///@todo deal with anything other than logger
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
                fmt_printhex(puts_fn, src, src_bytes, 16);
                break;
            
            // UTF-8 Hex-encoded data
            ///@todo have this print out hex in similar output to fmt_printhex
            case 0x03: 
                puts_fn((char*)payload);
                break;
            
            // Message with raw data
            case 0x04: {
                _output_binarylog(puts_fn, payload, length);
            } break;
                    
            // Message with UTF-8 data
            case 0x05: 
                _output_textlog(puts_fn, payload, length);
                break;
            
            // Message with Unicode (UTF-16) data
            // Not presently supported
            case 0x06: 
                _output_binarylog(puts_fn, payload, length);
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
    
    
    else {
        fmt_printhex(puts_fn, src, src_bytes, 16);
    }
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




