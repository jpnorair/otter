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
        byte    = hexlut0[(hexbuf[0]&0x7f)];
        byte   += hexlut1[(hexbuf[1]&0x7f)];
        *dst++  = byte;
    }

    return (int)(dst - start);
    
    pipe_gethex_SCRAP:
    return pollcode;
}
#endif //ifdef HEX_OVER_PIPE




static char* sub_findbreak(char* msg, size_t limit) {
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


static int sub_hexdump_raw(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
    static const char convert[] = "0123456789ABCDEF";
    char* dcurs;
    uint8_t* scurs;
    int rc;
    
    scurs = *src;
    dcurs = (char*)dst;
    
    while (srcsz-- != 0) {
        *dcurs++ = convert[(*scurs >> 4)];
        *dcurs++ = convert[(*scurs & 0x0f)];
        scurs++;
    }
    *dcurs  = 0;        // trailing null
    *src    = scurs;
    
    rc = (int)(dcurs-(char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += rc;
    }
    
    return rc;
}


// Any characters that are not valid hex are ignored
static int sub_passhex_loop(uint8_t* dst, uint8_t** src, size_t srcsz, size_t cols) {
    int i, j;
    char* scurs = (char*)*src;
    char* dcurs = (char*)dst;

    i = (int)cols;
    j = 2;
    while (srcsz-- != 0) {
        char x = *scurs++;
        
        if ((x >= 'a') && (x <= 'f')) {
            x -= ('a'-'A');
        }
        
        if (((x >= '0') && (x <= '9')) || ((x >= 'A') && (x <= 'F'))) {
            *dcurs++ = x;
            
            if (--j <= 0) {
                j = 2;
                if (cols != 0){
                    i--;
                    if ((i == 0) || (srcsz == 0)) {
                        i = (int)cols;
                        *dcurs++ = '\n';
                    }
                    else {
                        *dcurs++ = ' ';
                    }
                }
            }
        }
    }
    *dcurs  = 0;
    *src    = (uint8_t*)scurs;
    
    return (int)(dcurs - (char*)dst);
}



static int sub_passtext_loop(uint8_t* dst, uint8_t** src, size_t srcsz, size_t cols) {
    int i;
    char* dcurs     = (char*)dst;
    char* scurs     = (char*)*src;

    // In interactive mode, print everything, and add column breaks
    // In non-interactive mode:
    // - Certain characters are conditionally expanded (" -> \", ...)
    // - Certain characters are conditionally replaced (\n -> 'RS', ...)
    // - Certain characters are conditionally removed (\r, ...)
    i = (int)cols;
    while (srcsz-- != 0) {
        if (cliopt_getintf() == INTF_interactive) {
            *dcurs++ = *scurs++;
            if (cols != 0) {
                i--;
                if ((i == 0) || (srcsz == 0)) {
                    i = (int)cols;
                    *dcurs++ = '\n';
                }
            }
        }
        else {
            switch (*scurs) {
                case '\"':
                    if (cliopt_getformat() != FORMAT_Default) {
                        *dcurs++ = '\\';
                        *dcurs++ = '\"';
                    } break;
                    
                case '\n':
                    *dcurs++ = 31;
                    break;
                    
                case '\r':
                    break;
                    
                default:
                    *dcurs++ = *scurs;
                    break;
            }
            scurs++;
        }
    }
    *dcurs  = 0;
    *src    = (uint8_t*)scurs;
    
    return (int)(dcurs - (char*)dst);
}


static int sub_passhex(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz, size_t cols) {
    static const char* term_json = "\"";
    static const char* term_bintex = "]";
    int rc;
    char* dcurs;
    uint8_t* scurs;
    const char *strterm;
    
    dcurs = (char*)dst;
    scurs = *src;
    
    // The column printing feature is only available in Default (text) printing
    // to interactive console.
    switch (cliopt_getformat()) {
        case FORMAT_Hex:
            cols = 0;
            strterm = NULL;
            break;
            
        case FORMAT_Json:
            cols = 0;
            dcurs = stpcpy(dcurs, "\"");
            strterm = term_json;
            break;
            
        case FORMAT_Bintex:
            cols = 0;
            dcurs = stpcpy(dcurs, "[");
            strterm = term_bintex;
            break;
            
        default:
            if (cliopt_getintf() != INTF_interactive) {
                cols = 0;
            }
            strterm = NULL;
            break;
    }
    
    // Any characters that are not valid hex are ignored
    dcurs += sub_passhex_loop(dst, src, srcsz, cols);
    
    // Apply terminator string
    if (strterm != NULL) {
        dcurs = stpcpy(dcurs, strterm);
    }
    
    // Calculate written length and apply to results
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}


static int sub_printhex(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz, size_t cols) {
    static const char convert[] = "0123456789ABCDEF";
    static const char* term_json = "\"";
    static const char* term_bintex = "]";
    int i;
    int rc;
    char* dcurs;
    uint8_t* scurs;
    const char *strterm;
    
    dcurs = (char*)dst;
    scurs = *src;
    
    // The column printing feature is only available in Default (text) printing
    // to interactive console.
    switch (cliopt_getformat()) {
        case FORMAT_Hex:
            cols = 0;
            strterm = NULL;
            break;
            
        case FORMAT_Json:
            cols = 0;
            dcurs = stpcpy(dcurs, "\"");
            strterm = term_json;
            break;
            
        case FORMAT_Bintex:
            cols = 0;
            dcurs = stpcpy(dcurs, "[");
            strterm = term_bintex;
            break;
            
        default:
            if (cliopt_getintf() != INTF_interactive) {
                cols = 0;
            }
            strterm = NULL;
            break;
    }
    
    // Convert input to hex encoding.
    // If cols==0, it will be as a contiguous string, no whitespace.
    // If cols!=0, it will have whitespace separating bytes and newlines after
    // cols number of bytes.
    i = (int)cols;
    while (srcsz-- != 0) {
        *dcurs++ = convert[*scurs >> 4];
        *dcurs++ = convert[*scurs & 0x0f];
        scurs++;
        
        if (cols != 0) {
            i--;
            if ((i == 0) || (srcsz == 0)) {
                i = (int)cols;
                *dcurs++ = '\n';
            }
            else {
                *dcurs++ = ' ';
            }
        }
    }
    *dcurs  = 0;
    *src    = scurs;
    
    // Apply terminator string
    if (strterm != NULL) {
        dcurs = stpcpy(dcurs, strterm);
    }
    
    // Calculate written length and apply to results
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}


static int sub_printtext(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz, size_t cols) {
    int rc;
    char* dcurs = (char*)dst;
    
    // The column printing feature is only available in Default (text) printing
    // to interactive console.
    switch (cliopt_getformat()) {
        case FORMAT_Hex:
            return sub_printhex(dst, dst_accum, src, srcsz, cols/3);
            
        case FORMAT_Json:
        case FORMAT_Bintex:
            cols = 0;
            dcurs = stpcpy(dcurs, "\"");
            break;
        
        case FORMAT_Default:
        default:
//            if (cliopt_getintf() == INTF_interactive) {
//                memcpy(dst, (char*)*src, srcsz);
//                *src += srcsz;
//                dst += srcsz;
//                *dst = 0;
//                goto sub_printtext_END;
//            }
            break;
    }
    
    dcurs += sub_passtext_loop((uint8_t*)dcurs, src, srcsz, cols);
    
    // Terminator
    switch (cliopt_getformat()) {
        case FORMAT_Json:
        case FORMAT_Bintex:
            dcurs = stpcpy(dcurs, "\"");
            break;
        default:
            break;
    }
    
    sub_printtext_END:
    
    // Calculate written length and apply to results
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}


static int sub_log_binmsg(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
///@note subroutine, so inputs are expected to be valid -- no checks
    char* msgbreak;
    char* dcurs;
    char* front = (char*)*src;
    int length  = (int)srcsz;
    int rc;
    bool use_delim;
    bool use_newline;

    dcurs = (char*)dst;
    msgbreak = sub_findbreak(front, srcsz);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - front);
        
        // write out to the logger pipe
        ///@todo move this functionality into subscribers
        ppipelist_puthex("./pipes/log", front, msgbreak, length);
        
        // ppipe is going away, but if needed, it's possible to put a switch
        // here (via cliopts probably) to output binary instead of hex.
        //ppipelist_putbinary("./pipes/log", front, msgbreak, length);
        
        use_delim = false;
        use_newline = (cliopt_getintf() == INTF_interactive);
        switch (cliopt_getformat()) {
            //case FORMAT_Hex:  // Hex format handled in top-level formatter func
            case FORMAT_Json:
                use_delim = true;
                use_newline = false;
                
            case FORMAT_Default:
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                dcurs = stpcpy(dcurs, front);
                if (use_newline) {
                    dcurs = stpcpy(dcurs, "\n");
                    dcurs += sub_printhex((uint8_t*)dcurs, NULL, (uint8_t**)&msgbreak, length, 16);
                }
                else {
                    dcurs = stpcpy(dcurs, " ");
                    dcurs += sub_hexdump_raw((uint8_t*)dcurs, NULL, (uint8_t**)&msgbreak, length);
                }
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                break;
                
            case FORMAT_Bintex:
                dcurs += sub_printtext((uint8_t*)dcurs, NULL, (uint8_t**)&front, strlen(front), 0);
                dcurs += sub_printhex((uint8_t*)dcurs, NULL, (uint8_t**)&msgbreak, length, 0);
                break;
                
            default:
                break;
        }
    }
    
    *src += srcsz;
    
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}


static int sub_log_hexmsg(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
///@note subroutine, so inputs are expected to be valid -- no checks
    char* msgbreak;
    char* dcurs;
    char* front = (char*)*src;
    int length  = (int)srcsz;
    int rc;
    bool use_delim;
    bool use_newline;

    dcurs = (char*)dst;
    msgbreak = sub_findbreak(front, srcsz);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - front);
        
        // write out to the logger pipe
        ///@todo move this functionality into subscribers
        ppipelist_puttext("./pipes/log", front, msgbreak, length);
        
        use_delim = false;
        use_newline = (cliopt_getintf() == INTF_interactive);
        switch (cliopt_getformat()) {
            //case FORMAT_Hex:  // Hex format handled in top-level formatter func
            case FORMAT_Json:
                use_delim = true;
                use_newline = false;
                
            case FORMAT_Default:
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                dcurs = stpcpy(dcurs, front);
                dcurs = stpcpy(dcurs, use_newline ? "\n" : " ");
                dcurs += sub_passhex_loop((uint8_t*)dcurs, (uint8_t**)&msgbreak, length, use_newline ? 16 : 0);
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                break;
                
            case FORMAT_Bintex:
                dcurs += sub_printtext((uint8_t*)dcurs, NULL, (uint8_t**)&front, strlen(front), 0);
                dcurs = stpcpy(dcurs, "[");
                dcurs += sub_passhex_loop((uint8_t*)dcurs, (uint8_t**)&msgbreak, length, 0);
                dcurs = stpcpy(dcurs, "]");
                break;
                
            default:
                break;
        }
    }
    
    *src += srcsz;
    
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}



static int sub_log_textmsg(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
///@note subroutine, so inputs are expected to be valid -- no checks
    char* msgbreak;
    char* dcurs;
    char* front = (char*)*src;
    int length  = (int)srcsz;
    int rc;
    bool use_delim;
    bool use_newline;

    dcurs = (char*)dst;
    msgbreak = sub_findbreak(front, srcsz);
    
    if (msgbreak != NULL) {
        *msgbreak++ = 0;
        length -= (msgbreak - front);
        
        // write out to the logger pipe
        ///@todo move this functionality into subscribers
        ppipelist_puttext("./pipes/log", front, msgbreak, length);
        
        use_delim = false;
        use_newline = (cliopt_getintf() == INTF_interactive);
        switch (cliopt_getformat()) {
            //case FORMAT_Hex:  // Hex format handled in top-level formatter func
            case FORMAT_Json:
                use_delim = true;
                use_newline = false;
                
            case FORMAT_Default:
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                dcurs = stpcpy(dcurs, front);
                dcurs = stpcpy(dcurs, use_newline ? "\n" : " ");
                dcurs += sub_passtext_loop((uint8_t*)dcurs, (uint8_t**)&msgbreak, length, use_newline ? 80 : 0);
                if (use_delim) {
                    dcurs = stpcpy(dcurs, "\"");
                }
                break;
                
            case FORMAT_Bintex:
                dcurs += sub_printtext((uint8_t*)dcurs, NULL, (uint8_t**)&front, strlen(front), 0);
                dcurs += sub_printtext((uint8_t*)dcurs, NULL, (uint8_t**)&msgbreak, length, 0);
                break;
                
            default:
                break;
        }
    }
    
    *src += srcsz;
    
    rc = (int)(dcurs - (char*)dst);
    if (dst_accum != NULL) {
        *dst_accum += (size_t)rc;
    }
    
    return rc;
}



static int sub_printlog(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t length, uint8_t cmd) {
    char* dcurs = (char*)dst;
    
    if (cliopt_getformat() == FORMAT_Json) {
        if ((cmd == 1) || (cmd == 4) || (cmd == 5) || (cmd == 7)) {
            dcurs = stpcpy(dcurs, "\"fmt\":\"text\", \"dat\":");
        }
        else {
            dcurs = stpcpy(dcurs, "\"fmt\":\"hex\", \"dat\":");
        }
    }
    
    switch (cmd) {
        // "Raw" Data.  Print as hex
        case 0: dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, length, 16);
            break;
    
        // UTF-8 Unstructured Data
        case 1: dcurs += sub_printtext((uint8_t*)dcurs, dst_accum, src, length, 80);
            break;
            
        // Unicode (UTF-16) unstructured Data
        // (UTF-16 Not presently supported, treated as binary)
        case 2: dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, length, 16);
            break;
        
        // UTF-8 Hex-encoded data
        case 3: dcurs += sub_passhex((uint8_t*)dcurs, dst_accum, src, length, 80);
            break;
        
        // Message with raw data
        case 4: dcurs += sub_log_binmsg((uint8_t*)dcurs, dst_accum, src, length);
            break;
            
        // Message with UTF-8 data
        case 5: dcurs += sub_log_textmsg((uint8_t*)dcurs, dst_accum, src, length);
            break;
        
        // Message with Unicode (UTF-16) data
        // (UTF-16 Not presently supported, treated as binary)
        case 6: dcurs += sub_log_binmsg((uint8_t*)dcurs, dst_accum, src, length);
            break;
            
        // Message with UTF-8 encoded Hex data
        ///@todo have this print out hex in similar output to fmt_printhex
        case 7: dcurs += sub_log_hexmsg((uint8_t*)dcurs, dst_accum, src, length);
            break;
        
        // Hexify anything that isn't known
        default:
            dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, length, 16);
            break;
    }
    
    return (int)(dcurs - (char*)dst);
}



///@todo Deal with multiframe ALPs.
///      Currently, bypass ALPs that don't have Message-End bit set.
///      Multiframing will require building a local application buffer and
///      maintaining it for an ALP ID & device until Message End or next
///      Message Start is found.
int fmt_fprintalp(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
    int flags;
    int length;
    int rem_bytes;
    int cmd;
    int id;
    int rc = 0;
    char* dcurs = (char*)dst;
    uint8_t* scurs;

    /// Early exit conditions
    if ((dst == NULL) || (src == NULL) || (srcsz < 4) || (srcsz > 65535)) {
        return -1;
    }
    if (*src == NULL) {
        return -1;
    }
    
    scurs       = *src;
    rem_bytes   = (int)srcsz;
    
    // Guarantee null termination on the end of src
    // @note this is safe due to the way src buffer is allocated
    //scurs[srcsz]= 0;

    /// Look at ALP header for this record
    ///@todo Buffer it if no Message-End.  Discard Buffer on Message-Start.
    ///      Command value on last record (one with Message-End) will be
    ///      the Command that is used.
    flags       = *scurs++;
    length      = *scurs++;
    id          = *scurs++;
    cmd         = *scurs++;
    rem_bytes  -= 4;
    
    /// Validity Checks
    /// - Crop length to only the remaining bytes
    if (length > rem_bytes) {
        ///@note could squelch output for mismatched length
        length = (int)rem_bytes;
    }
    
///@todo remove pipes and move this feature into subscribers module
/// Send the ALP data to an appropriate output pipe.
/// If output pipe for this ALP isn't defined, nothing will happen.
/// The id/cmd bytes are sent as well
{   char str_alpid[8];
    snprintf(str_alpid, 7, "%d", id);
    ppipelist_puthex("./pipes/alp", str_alpid, (char*)(scurs-2), length+2);
}
    
    /// Framing
    /// Raw Hex: load ALP frame with no formatting/framing
    /// JSON: "alp":{"id":X, "cmd":Y, "len":Z, "fmt":["hex"|"text"|"obj"], "dat":[STRING|OBJECT]}
    /// Bintex: [Header (Hex)] [Payload (Hex)] or [Header (Hex)] "Payload (Text)"
    /// Default: Formatted Text per ID and command
    switch (cliopt_getformat()) {
        default:
        case FORMAT_Default:
            break;
            
        case FORMAT_Json:
            (*src) += 4;
            dcurs += sprintf(dcurs, "\"alp\":{\"id\":%u, \"cmd\":%u, \"len\":%u, ", id, cmd, length);
            break;
            
        case FORMAT_Bintex:
            ///@note No checks return code, because all error cases are already checked
            dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, 4, 0);
            break;
        
        case FORMAT_Hex:
            rc = sub_hexdump_raw(dst, dst_accum, src, srcsz);
            goto fmt_fprintalp_END;
    }
    
//fprintf(stderr, "flags=%02x length=%02x cmd=%02x id=%02x\n", flags, length, cmd, id);
    
    /// Length=0 / ID=0 exception.
    /// Simply ensure header is outputted.
    /// Hex, Bintex: Do nothing
    /// JSON: close the object
    /// Default: dump the header
    if ((length == 0) || (id == 0)) {
        switch (cliopt_getformat()) {
            case FORMAT_Json:
                // -2 is to eat the trailing ", " delimeter
                dcurs = stpcpy(dcurs-2, "}");
                break;
                
            case FORMAT_Default:
                ///@note No checks return code, because all error cases are already checked
                dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, 4, 0);
                break;
                
            default: // Do nothing
                break;
        }
        
        // Bypass garbage data on id == 0 && length != 0 case
        *src += length;
    }
    
    /// Locally handled ALP types
    /// id=0: Null Protocol -- Handled by length=0/id=0 exception above
    /// id=4: Logger
    /// ...
    else {
        switch (id) {
            // logger
            case 4: dcurs += sub_printlog((uint8_t*)dcurs, dst_accum, src, length, cmd);
                break;
                
            // everything else
           default: {
#           if OTTER_FEATURE(HBUILDER)
                //rc = hbuilder_fmtalp(puts_fn, (HBFMT_Type)cliopt_getformat(), id, cmd, scurs, length);
                rc = hbuilder_snfmtalp(dst, dst_accum, 2048, (HBFMT_Type)cliopt_getformat(), id, cmd, *src, length);
                if (rc >= 0) {
                    *src += length;
                }
#           else
                rc = -1;
#           endif
                /// Anything that falls through the cracks gets crapped-out as hex
                if (rc < 0) {
                    size_t output_bytes = (size_t)length;
                    switch (cliopt_getformat()) {
                        case FORMAT_Json:
                            dcurs = stpcpy(dcurs, "\"fmt\":\"hex\", \"dat\":");
                            break;
                        case FORMAT_Bintex:
                            break;
                        default:
                            output_bytes += 4;
                            break;
                    }
                    dcurs += sub_printhex((uint8_t*)dcurs, dst_accum, src, output_bytes, 16);
                }
            } break;
        }
        
        /// End Framing
        switch (cliopt_getformat()) {
            case FORMAT_Json:
                dcurs = stpcpy(dcurs, "}");
                break;
            
            default:
                break;
        }
    }
    
    fmt_fprintalp_END:
    
    /// At close, always manually take src past all utilized ALP protocol data
    *src = scurs + length;

    return (rc == 0) ? id : rc;
}



int fmt_printtext(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz, size_t cols) {
    int rc;
    
    if ((dst == NULL) || (src == NULL)) {
        return -1;
    }
    if (*src == NULL) {
        return 0;
    }
    
    rc = sub_printtext(dst, dst_accum, src, srcsz, cols);
    
    ///@todo could possibly do something here
    return rc;
}





int fmt_printhex(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz, size_t cols) {
    int rc;

    if ((dst == NULL) || (src == NULL)) {
        return -1;
    }
    if (*src == NULL) {
        return 0;
    }
    
    rc = sub_printhex(dst, dst_accum, src, srcsz, cols);
    
    ///@todo could possibly do something here
    return rc;
}





int fmt_hexdump_raw(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t srcsz) {
    if ((dst == NULL) || (src == NULL)) {
        return -1;
    }
    if (*src == NULL) {
        return 0;
    }
    
    return sub_hexdump_raw(dst, dst_accum, src, srcsz);
}


const char* fmt_hexdump_header(uint8_t* data) {
    static uint8_t hexdump_buf[32*3 + 1];
    
    /// @todo header inspection to determine length.
    ///       this entails looking at CONTROL field to see encryption type
    
    fmt_hexdump_raw(hexdump_buf, NULL, &data, 6);
    
    return (const char*)hexdump_buf;
}


const char* fmt_crc(int crcqual) {
    static const char invalid_CRC[] = "invalid CRC";
    static const char valid_CRC[] = "valid CRC";
    static const char decrypt_err[] = "Decryption Err";
    const char* retval;
    
    if (crcqual < 0) {
        retval = decrypt_err;
    }
    else if (crcqual != 0) {
        retval = invalid_CRC;
    }
    else {
        retval = valid_CRC;
    }
    return retval;
}


const char* fmt_time(time_t* tstamp) {
    static char time_buf[24];
    
    // convert to time using time.h library functions
    strftime(time_buf, 24, "%T", localtime(tstamp) );
    
    return time_buf;
}




