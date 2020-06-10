/*  Copyright 2017, JP Norair
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
  * @file       fdp.c
  * @author     JP Norair
  * @version    R102
  * @date       25 Nov 2017
  * @brief      Mode 2 File Data Protocol Response Formatter
  * @ingroup    ALP's
  *
  ******************************************************************************
  */


// Local Dependencies
#include "cmds.h"
#include "cmd_api.h"
#include "cmdutils.h"
#include "cliopt.h"
#include "dterm.h"
#include "otter_cfg.h"

// External Dependencies
#include <bintex.h>
#include <argtable3.h>
#include <hbutils.h>

// System Dependencies
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>


///@todo is this necessary here?  maybe it's in universar debug tool?
#ifdef __DEBUG__
#   define PRINTLINE()     fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__)
#   define DEBUGPRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#   define PRINTLINE()     do { } while(0)
#   define DEBUGPRINT(...) do { } while(0)
#endif

typedef struct {
    void** argtable;
    size_t argtable_size;
} fdp_parser_t;

typedef enum {
    ARG_help = 0,
    ARG_fcmd,
    ARG_block,
    ARG_prot,
    ARG_file,
    ARG_id,
    ARG_range,
    ARG_data,
    ARG_end,
    ARG_MAX
} ARGINDEX_t;

typedef const char* (*put_t)(hb_printer_t, const char*, const char*, const char*);



///@todo this might be encapsulated into some object in the future.
static void* fdp_handle = NULL;




int fdp_init(void** handle);
void fdp_free(void* handle);

int fdp_generate(   void* handle, 
                    uint8_t* dst, size_t* dst_bytes, size_t dstmax, 
                    uint8_t* src, int* src_bytes);








/// Top Level Command

int cmd_fdp(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    bool is_eos     = false;
    size_t bytesout = 0;
    int rc;
    
    /// dt == NULL is the init / deinit case.
    if (dth == NULL) {
        if (fdp_handle == NULL) {
            return fdp_init(&fdp_handle);
        }
        fdp_free(fdp_handle);
        fdp_handle = NULL;
        return 0;
    }
    
    INPUT_SANITIZE_FLAG_EOS(is_eos);
    
    rc = fdp_generate(fdp_handle, dst, &bytesout, dstmax, src, inbytes);
    if (rc <= 0) {
        if (rc == -2) {
            sprintf((char*)dst, "input error on character %zu", bytesout);
        }
        return rc;
    }

    if (cliopt_isverbose()) {
        char printbuf[80];
        sprintf(printbuf, "packetized %zu bytes", bytesout);
        dterm_send_cmdmsg(dth, "hbuilder", printbuf);
    }

    // This is the data to be sent over the I/O interface
    return (int)bytesout;
}




/// Generation Part
// ----------------------------------------------------------------------------

#define HELP_ELEMENT(TAB)   ((struct arg_lit*)((TAB)[ARG_help]))
#define FCMD_ELEMENT(TAB)   ((struct arg_str*)((TAB)[ARG_fcmd]))
#define BLOCK_ELEMENT(TAB)  ((struct arg_str*)((TAB)[ARG_block]))
#define ID_ELEMENT(TAB)     ((struct arg_int*)((TAB)[ARG_id]))
#define FILE_ELEMENT(TAB)   ((struct arg_lit*)((TAB)[ARG_file]))
#define PROT_ELEMENT(TAB)   ((struct arg_lit*)((TAB)[ARG_prot]))
#define RANGE_ELEMENT(TAB)  ((struct arg_str*)((TAB)[ARG_range]))
#define DATA_ELEMENT(TAB)   ((struct arg_str*)((TAB)[ARG_data]))
#define END_ELEMENT(TAB)    ((void*)((TAB)[ARG_end]))



void fdp_delete(void* out, void* in, size_t bytes_in);
void fdp_create(void* out, void* in, size_t bytes_in);
void fdp_read(void* out, void* in, size_t bytes_in);
void fdp_readall(void* out, void* in, size_t bytes_in);
void fdp_restore(void* out, void* in, size_t bytes_in);
void fdp_readhdr(void* out, void* in, size_t bytes_in);
void fdp_readperms(void* out, void* in, size_t bytes_in);
void fdp_writeover(void* out, void* in, size_t bytes_in);
void fdp_write(void* out, void* in, size_t bytes_in);
void fdp_writeperms(void* out, void* in, size_t bytes_in);


/// Binary Search Table for Commands
#define FDP_CMDCOUNT    (sizeof(fdp_commands)/sizeof(hbcmd_item_t))
#define _ISF            3
#define _ISS            2
#define _GFB            1

static const hbcmd_item_t fdp_commands[] = {
    { "del",      (void*)&fdp_delete,       {10,  1}},
    { "new",      (void*)&fdp_create,       {11,  6}},  // Requires extra data
    { "r",        (void*)&fdp_read,         {4,   5}},  
    { "r*",       (void*)&fdp_readall,      {12,  5}},  
    { "restore",  (void*)&fdp_restore,      {14,  1}},  
    { "rh",       (void*)&fdp_readhdr,      {8,   1}},  
    { "rp",       (void*)&fdp_readperms,    {0,   1}},  
    { "w",        (void*)&fdp_write,        {7,   5}},  // Requires extra data
    { "wo",       (void*)&fdp_writeover,    {6,   5}},  // Requires extra data
    { "wp",       (void*)&fdp_writeperms,   {3,   2}},  // Requires extra data
    { "z",        (void*)&fdp_restore,      {14,  1}},
};





static bool sub_validate_input(const hbcmd_item_t* fdp, size_t datbytes, uint8_t* args) {
    int remainder;
    
    // The Write command is special
    // Normal commands are valid with a multiple of the datbytes value
    if ((fdp->bundle.index == 6) || (fdp->bundle.index == 7)) {
        remainder = 0;
    }
    else {
        remainder = datbytes % fdp->bundle.argbytes;
    }
    
    return (remainder == 0) && (datbytes >= fdp->bundle.argbytes);
}



static int sub_bintex_proc(bool is_file, const char* str_input, char* dst, size_t dstmax) {
    int datbytes;
    FILE* fp;
    
    if (is_file) {
        fp = fopen(str_input, "r");
        if (fp != NULL) {
            datbytes = bintex_fs(fp, (unsigned char*)dst, (int)dstmax);
            fclose(fp);
        }
        else {
            datbytes = -1;
        }
    }
    else {
        datbytes = bintex_ss((unsigned char*)str_input, (unsigned char*)dst, (int)dstmax);
    }

    return datbytes;
}






int fdp_init(void** handle) {
    fdp_parser_t* parser;
    
    if (handle == NULL) {
        return -1;
    }
    
    parser = malloc(sizeof(fdp_parser_t));
    if (parser == NULL) {
        return -2;
    }

    parser->argtable_size   = ARG_MAX;
    parser->argtable        = malloc(sizeof(void*) * parser->argtable_size);
    if (parser->argtable == NULL) {
        free(parser);
        return -3;
    }
    
    ///@todo Consider making file ID an intN parameter
    {   struct arg_lit  *help   = arg_lit0(NULL,"help",                     "Print this help and exit");
        struct arg_str  *fcmd   = arg_str1(NULL, NULL, "fcmd",              "file command (del, new, r, r*, rh, rp, w, wo, wp, z)");
        struct arg_lit  *file   = arg_lit0("f","fread",                     "Data field is loaded as a file");
        struct arg_lit  *prot   = arg_lit0("p","prot",                      "Data field contains raw protocol");
        struct arg_str  *block  = arg_str0("b", "block", "gf|iss|isf",      "File Block.  Default: isf");
        struct arg_int  *id     = arg_int1(NULL, NULL, "<file id>",         "File ID");
        struct arg_str  *range  = arg_str0("r", "range", "X:Y",             "Access Range, X bytes to Y bytes.  Default: 0:65535");
        struct arg_str  *data   = arg_str0(NULL, NULL, "<bintex|filepath>", "Bintex formatted data field, either as text on command line or path to file.");    
        
        parser->argtable[ARG_help]  = (void*)help;
        parser->argtable[ARG_fcmd]  = (void*)fcmd;
        parser->argtable[ARG_block] = (void*)block;
        parser->argtable[ARG_id]    = (void*)id;
        parser->argtable[ARG_file]  = (void*)file;
        parser->argtable[ARG_prot]  = (void*)prot;
        parser->argtable[ARG_range] = (void*)range;
        parser->argtable[ARG_data]  = (void*)data;
        parser->argtable[ARG_end]   = (void*)arg_end(10);
    }
    
    if (arg_nullcheck(parser->argtable) != 0) {
        fdp_free(parser);
        return -4;
    }

    *handle = parser;
    return 0;
}

void fdp_free(void* handle) {
    fdp_parser_t* parser;
    
    if (handle != NULL) {
        parser = handle;
        arg_freetable(parser->argtable, parser->argtable_size);
        free(parser->argtable);
        free(parser);
    }
}




// Callable processing function
int fdp_generate(   void* handle, 
                    uint8_t* dst, size_t* dst_bytes, size_t dstmax, 
                    uint8_t* src, int* src_bytes
                ) {

    // Input processing variables
    size_t      id_list_size;
    int*        id_list;
    uint8_t     block_id;
    uint16_t    r_start, r_end;

    // Command line string processing variables
    uint8_t     term_byte;
    int         argc;
    int         nerrors;
    const char* cmdname = "file";
    char**      argv;
    fdp_parser_t* parser;
    
    // Output generation variables
    const hbcmd_item_t*  fdp;
    uint8_t*    record;
    uint8_t*    payload;
    int         out_val;
    
    /// 1. None of the input pointers can be NULL.  Handle should be checked
    ///    via hbuilder_runcmd()
    if ((dst == NULL) || (dst_bytes == NULL) || 
        (src == NULL) || (src_bytes == NULL)) {
        return -1;
    }

    /// 2. The src is a string that may or may not be null-terminated, but the
    ///    length in bytes is known via *src_bytes.  The byte after the length
    ///    is temporarily null-terminated (reverted back at end of parsing)
    term_byte       = src[*src_bytes];
    src[*src_bytes] = 0;
    record          = dst;
    payload         = &dst[4];
    dst             = &dst[4];
    dstmax         -= 4;
    record[0]       = 0xC0;
    record[1]       = 0;
    record[2]       = 1;
    record[3]       = 0x80;
    
    /// 3. First, create an argument vector from the input string.
    ///    hbutils_parsestring will treat all bintex containers as whitespace-safe.
    parser  = handle;
    argc    = hbutils_parseargv(&argv, cmdname, (char*)src, (char*)src, (size_t)*src_bytes);
    if (argc <= 1) {
        out_val = -2;
        goto fdp_generate_END;
    }
    nerrors = arg_parse(argc, argv, parser->argtable );
    
    /// 4. Print command specific help
    /// @todo this is currently just generic help
    if (HELP_ELEMENT(parser->argtable)->count > 0) {
        fprintf(stderr, "Usage: %s [cmd]", cmdname);
        arg_print_syntax(stderr, parser->argtable, "\n");
        arg_print_glossary(stderr, parser->argtable, "  %-25s %s\n");
        out_val = 0;
        goto fdp_generate_END;
    }
    
    /// Print errors, if errors exist
    if (nerrors > 0) {
        char printbuf[32];
        snprintf(printbuf, sizeof(printbuf), "%s [cmd]", cmdname);
        arg_print_errors(stderr, parser->argtable[parser->argtable_size-1], printbuf);
        out_val = -3;
        goto fdp_generate_END;
    }
    
    /// Command code should match one of the command strings.
    if (FCMD_ELEMENT(parser->argtable)->count > 0) {
        DEBUGPRINT("FCmd flag found\n");
        fdp = hbutils_cmdsearch(fdp_commands, FCMD_ELEMENT(parser->argtable)->sval[0], FDP_CMDCOUNT);
        if (fdp != NULL) {
            record[3] |= fdp->bundle.index;
            goto fdp_generate_PROT;
        }
    }
    out_val = -4;
    goto fdp_generate_END;
    
    /// Check for protocol flag (-p, --prot) which means next argument is 
    /// just the straight-up bintex data, and we bypass all the parsing below.
    fdp_generate_PROT:
    if (PROT_ELEMENT(parser->argtable)->count > 0) {
        DEBUGPRINT("Prot flag found\n");
        if (DATA_ELEMENT(parser->argtable)->count > 0) {
            DEBUGPRINT("Prot Data: %s\n", DATA_ELEMENT(parser->argtable)->sval[0]);
            out_val = sub_bintex_proc( (bool)(FILE_ELEMENT(parser->argtable)->count > 0), 
                                        DATA_ELEMENT(parser->argtable)->sval[0], 
                                        (char*)payload, 
                                        (int)dstmax );
            goto fdp_generate_WRITEOUT;
        }
        else {
            out_val = -5;
            goto fdp_generate_END;
        }
    }
    
    /// This block is only for debug
    if (FILE_ELEMENT(parser->argtable)->count > 0) {
        DEBUGPRINT("fread flag found\n");
    }
    
    // ------------------------------------------------------------------------------
    // End of generic boilerplate for generator input functions
    // ------------------------------------------------------------------------------
    
    
    
    /// Check for block flag (-b, --block), which specifies fs block
    /// Default is isf.
    block_id = 3 << 4;      // default: isf
    if (BLOCK_ELEMENT(parser->argtable)->count > 0) {
        DEBUGPRINT("Block flag encountered: %s\n", BLOCK_ELEMENT(parser->argtable)->sval[0]);
        if (strncmp(BLOCK_ELEMENT(parser->argtable)->sval[0], "isf", 3) == 0) {
            block_id = 3 << 4;
        }
        else if (strncmp(BLOCK_ELEMENT(parser->argtable)->sval[0], "iss", 3) == 0) {
            block_id = 2 << 4;
        }
        else if (strncmp(BLOCK_ELEMENT(parser->argtable)->sval[0], "gfb", 3) == 0) {
            block_id = 1 << 4;
        }
        else {
            out_val = -6;
            goto fdp_generate_END;
        }
    }
    record[3] |= block_id;
    
    /// File ID must exist if "prot" flag is not used
    /// We want to sanitize the input IDs to make sure they fit inside the rules.
    /// The write command (7) only accepts a single ID
    if (ID_ELEMENT(parser->argtable)->count > 0) {
        DEBUGPRINT("ID arg encountered: %d\n", ID_ELEMENT(parser->argtable)->ival[0]);
        id_list_size = ID_ELEMENT(parser->argtable)->count;
        
        if (((id_list_size > 1) && ((fdp->bundle.index == 7) || (fdp->bundle.index == 6)))
        ||  ((id_list_size * fdp->bundle.argbytes) > dstmax)) {
            out_val = -7;
            goto fdp_generate_END;
        }
        
        id_list = ID_ELEMENT(parser->argtable)->ival;
        for (int i=0; i<ID_ELEMENT(parser->argtable)->count; i++) {
            id_list[i] &= 255;
        }
    }
    else {
        out_val = -7;
        goto fdp_generate_END;
    }
    
    /// Range is optional.  Default is maximum file range (0:)
    r_start = 0;
    r_end   = 65535;
    if (RANGE_ELEMENT(parser->argtable)->count > 0) {
        char *start, *end, *ctx;
        DEBUGPRINT("Range flag encountered: %s\n", RANGE_ELEMENT(parser->argtable)->sval[0]);
        start   = strtok_r((char*)RANGE_ELEMENT(parser->argtable)->sval[0], ":", &ctx);
        end     = strtok_r(NULL, ":", &ctx);

        if (start != NULL) {
            r_start = (uint16_t)atoi(start);
        }
        if (end != NULL) {
            r_end = (uint16_t)atoi(end);
        }
    }

    /// Load CLI inputs into appropriate output offsets of binary protocol.
    /// Some commands take additional loose data inputs, which get placed into
    /// the binary protocol output after this stage
    for (int i=0; i<id_list_size; i++) {
        if (fdp->bundle.argbytes == 5) {
            dst[4] = (uint8_t)(r_end & 255);
            dst[3] = (uint8_t)(r_end >> 8);
            dst[2] = (uint8_t)(r_start & 255);
            dst[1] = (uint8_t)(r_start >> 8);
        }
        dst[0]  = (uint8_t)id_list[i];
        dst    += fdp->bundle.argbytes;
        dstmax -= fdp->bundle.argbytes;
    }
   
    /// Commands with 3, 6, 7, 11  additional data parameter
    /// 3 - write perms
    /// 6 - writeover data      : Special, has an additional data payload
    /// 7 - write data          : Special, has an additional data payload
    /// 11 - create empty file
    if ((fdp->bundle.index == 3)
    ||  (fdp->bundle.index == 6)
    ||  (fdp->bundle.index == 7)
    ||  (fdp->bundle.index == 11)) {
        uint8_t datbuffer[256];
        int     datbytes = 0;
        
        if (DATA_ELEMENT(parser->argtable)->count > 0) {
            DEBUGPRINT("Data: %s\n", DATA_ELEMENT(parser->argtable)->sval[0]);
            datbytes = sub_bintex_proc( (bool)(FILE_ELEMENT(parser->argtable)->count > 0), 
                                        DATA_ELEMENT(parser->argtable)->sval[0], 
                                        (char*)datbuffer, 250 );
        }
        if (datbytes <= 0) {
            out_val = -8;
            goto fdp_generate_END;
        }
        
        if ((fdp->bundle.index == 6) || (fdp->bundle.index == 7)) {
            void* dst0;
            
            if (RANGE_ELEMENT(parser->argtable)->count > 0) {
                if ((r_end - r_start) < datbytes) {
                    datbytes = (r_end - r_start);
                }
            }
            if (datbytes > dstmax) {
                datbytes = (int)dstmax;
            }
            
            r_end       = r_start + (uint16_t)datbytes;
            dst[-1]     = (uint8_t)(r_end & 255);
            dst[-2]     = (uint8_t)(r_end >> 8);
            dstmax     -= datbytes;
            dst0        = (void*)dst;
            dst        += datbytes;
            
            memcpy(dst0, datbuffer, datbytes);
        }
        else {
            uint8_t* p;
            int i, j;
            
            for (i=0, j=0, p=payload; i<id_list_size; ) {
                // payload[0] is always location of file-id, already loaded
                p[1] = datbuffer[j++];
                if (fdp->bundle.index == 11) {
                    p[2] = 0;
                    p[3] = 0;
                    p[4] = datbuffer[j++];
                    p[5] = datbuffer[j++];
                }
                p += fdp->bundle.argbytes;
            }
        }
    }
    
    /// For all command-line generated payloads, the payload length is finalized here
    out_val = (int)(dst - payload);
    
    
    
    fdp_generate_WRITEOUT:
    // ------------------------------------------------------------------------------
    // Start of generic boilerplate for generator output functions
    // ------------------------------------------------------------------------------

    /// Binary payload output is completely loaded at this time.
    /// Make sure it is within bounds required, and then do a quick validation step 
    /// to make sure it is well-formed.
    if (out_val > 255) {
        out_val = -9;
        goto fdp_generate_END;
    }
    if (sub_validate_input(fdp, out_val, payload) == false) {
        out_val = -10;
        goto fdp_generate_END;
    }
  
    /// record[1] should hold the length of the payload.  
    /// The payload is the output bytes minus size of header (4)
    record[1]   = (uint8_t)out_val;
    out_val    += 4;
    *dst_bytes  = out_val;

    /// The end.  Replace the character that was temporarily made '/0'
    /// Also, print generic help statement on error.
    fdp_generate_END:
    src[*src_bytes] = term_byte;
    if (out_val < 0) {
        fprintf(stderr, "Err %i, Usage: %s [cmd]", out_val, cmdname);
        arg_print_syntax(stderr, parser->argtable, "\n");
        arg_print_glossary(stderr, parser->argtable, "  %-25s %s\n");
    }
    hbutils_freeargv(argv);
    
    return out_val;
}





/// Function generators.  Currently Unused
void fdp_delete(void* out, void* in, size_t bytes_in) { };
void fdp_create(void* out, void* in, size_t bytes_in) { };
void fdp_read(void* out, void* in, size_t bytes_in) { };
void fdp_readall(void* out, void* in, size_t bytes_in) { };
void fdp_restore(void* out, void* in, size_t bytes_in) { };
void fdp_readhdr(void* out, void* in, size_t bytes_in) { };
void fdp_readperms(void* out, void* in, size_t bytes_in) { };
void fdp_writeover(void* out, void* in, size_t bytes_in) { };
void fdp_write(void* out, void* in, size_t bytes_in) { };
void fdp_writeperms(void* out, void* in, size_t bytes_in) { };


// ----------------------------------------------------------------------------
/// End of Generation Part









/// Formatter Part
// -----------------------------------------------------------------------------


///@todo have an adaptive way to determine the console size
#define _COLS   24


// sorted list of supported commands
#define _ISF    3
#define _ISS    2
#define _GFB    1


static const char* block_names = \
    "NUL\0" \
    "GFB\0" \
    "ISS\0" \
    "ISF\0" \
    "NUL\0" \
    "NUL\0" \
    "NUL\0" \
    "NUL\0";

static const char* msg_err  = "Error";
static const char* msg_ack  = "Ack";
static const char* msg_rd   = "Read Data";
static const char* msg_rp   = "Read Permissions";
static const char* msg_rh   = "Read Header";
static const char* msg_rhd  = "Read Header+Data";

static const char hexconv[16]  = "0123456789ABCDEF";


#define RELIMIT(CNT, MARKER) \
    do { dst += CNT; limit -= CNT; if (limit <= 0) { goto MARKER; } } while(0)


static int _get_u8(const char** src, const char* end) {
    int extract;

    if ((end-*src) >= 1) {
        extract = (255 & **src);
        (*src)++;
    }
    else {
        extract = -1;
    }
    
    return extract;
}

static int _get_u16(const char** src, const char* end) {
    int extract;

    if ((end-*src) >= 2) {
        extract = (255 & **src) << 8;   (*src)++;
        extract|= (255 & **src);        (*src)++;
    }
    else {
        extract = -1;
    }
    
    return extract;
}



///@todo these hex output functions are the same in both ASAPI and FDP formatters

static int sub_dumphex(char* dst, int limit, const char** src, size_t srcsz) {
    char* start = dst;
    
    while ((srcsz-- != 0) && (limit >= 2)) {
        char byte = **src;
        (*src)++;
        *dst++ = hexconv[(byte >> 4) & 0x0f];
        *dst++ = hexconv[byte & 0x0f];
    }
    
    return (int)(dst - start);
}


static int sub_printhex(char* dst, int limit, const char** src, size_t print_bytes, size_t cols) {
    int i;
    char* start = dst;
    int cnt;
    
    i = (int)cols;
    while (print_bytes-- != 0) {
        char hexstr[4] = {0, 0, ' ', 0};
        char byte;
        
        byte        = **src;
        (*src)++;
        hexstr[0]   = hexconv[(byte >> 4) & 0x0f];
        hexstr[1]   = hexconv[byte & 0x0f];
        i--;
        
        if ((i == 0) && (print_bytes != 0)) {
            i = (int)cols;
            hexstr[2] = '\n';
        }
        
        cnt = snprintf(dst, limit, "%s", hexstr);
        RELIMIT(cnt, sub_printhex_END);
    }
    
    sub_printhex_END:
    return (int)(dst-start);
}

//snprintf(buf, sizeof(buf), "range:{pos:%hu, size:%hu}, ", offset, bytes);


static int sub_offsetlength(size_t* dlength, char* dst, int limit, const char** src) {
    uint16_t offset;
    uint16_t bytes;
    char* start = dst;
    int cnt;
    
    offset  = (255 & **src) * 256;  (*src)++;
    offset += (255 & **src);        (*src)++;
    bytes   = (255 & **src) * 256;  (*src)++;
    bytes  += (255 & **src);        (*src)++;
    *dlength = (size_t)bytes;
    
    cnt = snprintf(dst, limit, "[%hu:%u] ", offset, offset+bytes);
    RELIMIT(cnt, sub_offsetlength_END);

    sub_offsetlength_END:
    return (int)(dst - start);
}


static int sub_lengthalloc(char* dst, int limit, const char** src) {
    uint16_t length;
    uint16_t alloc;
    char* start = dst;
    int cnt;
    
    length  = (255 & **src) * 256;  (*src)++;
    length += (255 & **src);        (*src)++;
    alloc   = (255 & **src) * 256;  (*src)++;
    alloc  += (255 & **src);        (*src)++;
    
    cnt = snprintf(dst, limit, "len:%hu, max:%hu ", length, alloc);
    RELIMIT(cnt, sub_lengthalloc_END);
    
    sub_lengthalloc_END:
    return (int)(dst - start);
}


static int sub_legend(char* dst, int limit, const char** src) {
    uint8_t code;
    int cnt;
    
    code = **src;
    (*src)++;
    
    cnt = snprintf(dst, limit, "File %hhu: ", code);
    
    return cnt;
}






// Return File Permission
// 2 bytes: [file id (1), perms (1)]
static int put_idperm(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    char buf[12];
    int cnt;
    char* start = dst;
    uint8_t perms;
    
    if ((src_end - *src) >= 2) {
        cnt = sub_legend(dst, limit, src);
        RELIMIT(cnt, put_idperm_END);
        
        perms   = **src;
        (*src)++;
        buf[0]  = (perms & 0x80) ? 'C' : '-';
        buf[1]  = (perms & 0x40) ? 'X' : '-';
        buf[2]  = (perms & 0x20) ? 'r' : '-';
        buf[3]  = (perms & 0x10) ? 'w' : '-';
        buf[4]  = (perms & 0x08) ? 'x' : '-';
        buf[5]  = (perms & 0x04) ? 'r' : '-';
        buf[6]  = (perms & 0x02) ? 'w' : '-';
        buf[7]  = (perms & 0x01) ? 'x' : '-';
        buf[8]  = 0;
        
        cnt = snprintf(dst, limit, "%s", buf);
        RELIMIT(cnt, put_idperm_END);
    }
    else {
        cnt = snprintf(dst, limit, "Parsing Error, protocol header malformed.");
        *src = src_end;
        RELIMIT(cnt, put_idperm_END);
    }
    
    put_idperm_END:
    return (int)(dst-start);
}

static int put_idperm_json(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int id, mod;
    int cnt;
    char* start = dst;
    
    id  = _get_u8(src, src_end);
    mod = _get_u8(src, src_end);
    
    cnt = snprintf(dst, limit, "\"parse\":{\"type\":\"file\", \"op\":\"rp\", \"info\":{\"block\":\"%s\", \"id\":%i, \"mod\":%i}}",
                    block, id, mod);
    RELIMIT(cnt, put_idperm_json_END);

    put_idperm_json_END:
    return (int)(dst - start);
}






// Return File Data
// 5 bytes + Data: [file id (1), offset (2), returned (2)]
static int put_filedata(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    size_t bytes;
    int span;
    int cnt;
    char* start = dst;

    if ((src_end - *src) < 5) {
        goto put_filedata_ERROR;
    }
    
    cnt = sub_legend(dst, limit, src);
    RELIMIT(cnt, put_filedata_END);
    
    cnt = sub_offsetlength(&bytes, dst, limit, src);
    RELIMIT(cnt, put_filedata_END);
    
    *dst = '\n';
    RELIMIT(1, put_filedata_END);
    
    // Make sure amount of bytes to read is not beyond the amount of bytes in input.
    span = (int)(src_end - *src);
    if (bytes > span) {
        goto put_filedata_ERROR;
    }
    
    cnt = sub_printhex(dst, limit, src, bytes, _COLS);
    RELIMIT(cnt, put_filedata_END);
    
    put_filedata_END:
    return (int)(dst - start);
    
    put_filedata_ERROR:
    cnt = snprintf(dst, limit, "Parsing Error: offset/length doesn't match input.");
    dst += cnt;
    *src = src_end;
    return (int)(dst - start);
}


static int put_filedata_json(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int id, pos, size;
    int cnt;
    char* start = dst;
    
    id  = _get_u8(src, src_end);
    pos = _get_u16(src, src_end);
    size= _get_u16(src, src_end);
    

    cnt = snprintf(dst, limit,
                    "\"parse\":{\"type\":\"file\", \"op\":\"r\", " \
                    "\"info\":{\"block\":\"%s\", \"id\":%i, \"pos\":%i, \"size\":%i}, " \
                    "\"payload\":\"",
                    block, id, pos, size );
    RELIMIT(cnt, put_filedata_json_END);
    
    cnt = sub_dumphex(dst, limit, src, size);
    RELIMIT(cnt, put_filedata_json_END);

    cnt = snprintf(dst, limit, "\"}");
    RELIMIT(cnt, put_filedata_json_END);

    put_filedata_json_END:
    return (int)(dst - start);
}


// Return File Header
// 6 bytes: [file id (1), perms (1), length (2), alloc (2)]
static int put_fileheader(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int cnt;
    char* start = dst;

    if ((src_end - *src) < 5) {
        goto put_fileheader_ERR;
    }
    
    cnt = put_idperm(block, dst, limit, src, src_end);
    RELIMIT(cnt, put_fileheader_END);
    
    cnt = sub_lengthalloc(dst, limit, src);
    RELIMIT(cnt, put_fileheader_END);

    put_fileheader_END:
    return (int)(dst - start);

    put_fileheader_ERR:
    cnt = snprintf(dst, limit, "Parsing Error: protocol header malformed.");
    dst += cnt;
    *src = src_end;
    return (int)(dst - start);
}


static int put_fileheader_json(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int id, mod, len, max;
    int cnt;
    char* start = dst;
    
    id  = _get_u8(src, src_end);
    mod = _get_u8(src, src_end);
    len = _get_u16(src, src_end);
    max = _get_u16(src, src_end);
    
    cnt = snprintf( dst, limit,
                    "\"parse\":{\"type\":\"file\", \"op\":\"rh\", " \
                    "\"info\":{\"block\":\"%s\", \"id\":%i, \"mod\":%i, \"len\":%i, \"max\":%i}}",
                    block, id, mod, len, max );
    RELIMIT(cnt, put_fileheader_json_END);
    
    put_fileheader_json_END:
    return (int)(dst - start);
}


// Return File Header + Data
// 10 bytes: [file id (1), perms (1), length (2), alloc (2), offset (2), returned (2)]
static int put_fileheaderdata(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    size_t bytes;
    int span;
    int cnt;
    char* start = dst;
    
    if ((src_end - *src) < 10) {
        goto put_fileheaderdata_ERR;
    }
    
    cnt = sub_legend(dst, limit, src);
    RELIMIT(cnt, put_fileheaderdata_END);
    
    cnt = sub_lengthalloc(dst, limit, src);
    RELIMIT(cnt, put_fileheaderdata_END);
    
    cnt = sub_offsetlength(&bytes, dst, limit, src);
    RELIMIT(cnt, put_fileheaderdata_END);
    
    *dst = '\n';
    RELIMIT(1, put_fileheaderdata_END);

    // Make sure amount of bytes to read is not beyond the amount of bytes in input.
    span = (int)(src_end - *src);
    if (bytes > span) {
        goto put_fileheaderdata_ERR;
    }
    
    cnt = sub_printhex(dst, limit, src, bytes, _COLS);
    RELIMIT(cnt, put_fileheaderdata_END);
    
    put_fileheaderdata_END:
    return (int)(dst - start);
    
    put_fileheaderdata_ERR:
    cnt = snprintf(dst, limit, "Parsing Error: protocol header.");
    dst += cnt;
    *src = src_end;
    return (int)(dst - start);
}


static int put_fileheaderdata_json(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int id, mod, len, max, pos, size;
    int cnt;
    char* start = dst;
    
    id  = _get_u8(src, src_end);
    mod = _get_u8(src, src_end);
    len = _get_u16(src, src_end);
    max = _get_u16(src, src_end);
    pos = _get_u16(src, src_end);
    size= _get_u16(src, src_end);
    
    cnt = snprintf(dst, limit,
            "\"parse\":{\"type\":\"file\", \"op\":\"r*\", " \
            "\"file\":{\"block\":\"%s\", \"id\":%i, \"mod\":%i, \"len\":%i, \"max\":%i, \"pos\":%i, \"size\":%i}, " \
            "\"payload\":\"",
            block, id, mod, len, max, pos, size );
    RELIMIT(cnt, put_fileheaderdata_json_END);
    
    cnt = sub_dumphex(dst, limit, src, size);
    RELIMIT(cnt, put_fileheaderdata_json_END);
    
    cnt = snprintf(dst, limit, "\"}");
    RELIMIT(cnt, put_fileheaderdata_json_END);
    
    put_fileheaderdata_json_END:
    return (int)(dst - start);
}






// Error or write-response
// 2 bytes: [filed id (1), code (1)]
static int put_error(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int cnt;
    unsigned char code;
    char* start = dst;
    
    if ((src_end - *src) >= 2) {
        cnt = sub_legend(dst, limit, src);
        RELIMIT(cnt, put_error_END);
        
        code = **src; (*src)++;
        cnt = snprintf(dst, limit, "Code %hhu", code);
    }
    else {
        cnt = snprintf(dst, limit, "Parsing Error: protocol header malformed.");
        *src = src_end;
    }
    dst += cnt;

    put_error_END:
    return (int)(dst - start);
}


static int put_error_json(const char* block, char* dst, int limit, const char** src, const char* src_end) {
    int id, err, cnt;
    char* start = dst;
    
    id  = _get_u8(src, src_end);
    err = _get_u8(src, src_end);
    
    cnt = snprintf(dst, limit, "\"parse\":{\"type\":\"file\", \"op\":\"%s\", \"file\":{\"block\":\"%s\", \"id\":%i}",
                        (err) ? "err":"ack", block, id);
    RELIMIT(cnt, put_error_json_END);
    
    if (err == 0) {
        strcpy(dst, "}");
        cnt = 1;
    }
    else {
        cnt = snprintf(dst, limit, ", \"err\":{\"code\":%i}}", err);
    }
    dst += cnt;
    
    put_error_json_END:
    return (int)(dst - start);
}




static int stdout_puts(char* s) {
    return fputs((const char*)s, stdout);
}





// Callable processing function
int fdp_formatter(char* dst, size_t* dst_accum, size_t dst_limit,
                    HBFMT_Type fmt, uint8_t cmd, uint8_t** src, size_t srcsz) {
    int rc;
    int cnt;
    int limit;
    char* start;
    const char* op;
    const char* bname;
    const char* src_end;
    int (*put)(const char*, char*, int, const char**, const char*);
    
    //fprintf(stderr, "-- payload_len = %zu\n", payload_len);
    
    // Input checks.
    if ((dst == NULL) || (src == NULL)){
        return -1;
    }
    if ((*src == NULL) || (srcsz == 0)) {
        return -1;
    }
    
    start   = dst;
    bname   = block_names + ((cmd & 0x70) >> 2);
    src_end = (const char*)*src + srcsz;
    rc      = 0;
    limit   = (int)dst_limit;
    
    // determine output response
    if (fmt == HBFMT_Hex) {
        cnt = sub_dumphex(dst, limit, (const char**)src, srcsz);
        RELIMIT(cnt, fdp_formatter_END);
    }
    
    else if (fmt == HBFMT_Json) {
        switch (cmd & 15) {
        // See default format handling for better comments
        case 1:     put = &put_idperm_json;         break;
        case 5:     put = &put_filedata_json;       break;
        case 9:     put = &put_fileheader_json;     break;
        case 13:    put = &put_fileheaderdata_json; break;
        case 15:    put = &put_error_json;          break;
        default:
            put = NULL;
            rc  = -2;
            break;
        }
        if (put != NULL) {
            while ((const char*)*src < src_end) {
                cnt = put(bname, dst, limit, (const char**)src, src_end);
                RELIMIT(cnt, fdp_formatter_END);
            }
        }
    }
    
    else {
        switch (cmd & 15) {
        
        // Return File Permission
        // 2 bytes: [file id (1), perms (1)]
        case 1:     op  = msg_rp;   put = &put_idperm;          break;
        
        // Return File Data
        // 5 bytes + Data: [file id (1), offset (2), returned (2)]
        case 5:     op  = msg_rd;   put = &put_filedata;        break;
        
        // Return File Header
        // 6 bytes: [file id (1), perms (1), length (2), alloc (2)]
        case 9:     op  = msg_rh;   put = &put_fileheader;      break;
        
        // Return File Header + Data
        // 10 bytes: [file id (1), perms (1), length (2), alloc (2), offset (2), returned (2)]
        case 13:    op  = msg_rhd;  put = &put_fileheaderdata;  break;
        
        // error or write-response
        // 2 bytes: [filed id (1), code (1)]
        case 15:    if ((*src)[1] == 0) {
                        op = msg_ack;
                    }
                    else {
                        op = msg_err;
                        rc = -1;
                    }
                    put = &put_error;
                    break;
                
        default:    op  = NULL; put = NULL; rc  = -2;   break;
        }
        
        if (op != NULL) {
            cnt = snprintf(dst, limit, "%s on %s ", op, bname);
            RELIMIT(cnt, fdp_formatter_END);
        
            while ((const char*)*src < (const char*)src_end) {
                cnt = put(bname, dst, limit-1, (const char**)src, src_end);
                RELIMIT(cnt, fdp_formatter_END);
                *dst++ = '\n'; limit--;
            }
            // strip last newline
            dst--;  limit++;
        }
        else {
            cnt = snprintf(dst, limit, "Error: Received non-compliant FDP Return");
            RELIMIT(cnt, fdp_formatter_END);
        }
    }
    
    rc = (int)(dst-start);
    
    fdp_formatter_END:
    *dst_accum = (size_t)(dst - start);
    
    return rc;
}

// ----------------------------------------------------------------------------
/// End of Formatter Part

