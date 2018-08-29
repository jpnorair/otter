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

// Local Headers
#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_cfg.h"
//#include "test.h"
#include "user.h"


// HB Headers/Libraries
#include <bintex.h>
#include <cmdtab.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


// HBuilder is part of Haystack HDO and it is not open source as of 08.2017.
// HBuilder provides a library of DASH7/OpenTag communication API functions 
// that are easy to use.
#if OTTER_FEATURE(HBUILDER)
#   include <hbuilder.h>
#endif



/// Variables used across shell commands




#define HOME_PATH_MAX           1024
char home_path[HOME_PATH_MAX]   = "~/";
int home_path_len               = 2;


/// This key is hardcoded for AES128
///@todo have a way to make this dynamic based on cli parameters, or mode params
static uint8_t user_key[16];





static int hexstr_to_uint8(uint8_t* dst, const char* src) {
    size_t chars;
    int bytes;
    
    chars   = strlen(src);
    chars >>= 1;    // round down to nearest even
    bytes   = (int)chars;
    
    while (chars != 0) {
        char c;
        uint8_t a, b;
        chars--;
        
        c = *src++;
        if      (c >= '0' && c <= '9')  a = c - '0';
        else if (c >= 'A' && c <= 'F')  a = 10 + c - 'A';
        else if (c >= 'a' && c <= 'f')  a = 10 + c - 'a';
        else    a = 0;
        
        c = *src++;
        if      (c >= '0' && c <= '9')  b = c - '0';
        else if (c >= 'A' && c <= 'F')  b = 10 + c - 'A';
        else if (c >= 'a' && c <= 'f')  b = 10 + c - 'a';
        else    b = 0;
        
        *dst++ = (a << 4) + b;
    }
    
    return bytes;
}


static int base64_to_uint8(uint8_t* dst, const char* src) {
///@todo build this function on a rainy day -- probably reference a BASE64 lib.
    return 0;
}



uint8_t* sub_markstring(uint8_t** psrc, int* search_limit, int string_limit) {
    size_t      code_len;
    size_t      code_max;
    uint8_t*    cursor;
    uint8_t*    front;
    
    /// 1. Set search limit on the string to mark within the source string
    code_max    = (*search_limit < string_limit) ? *search_limit : string_limit; 
    front       = *psrc;
    
    /// 2. Go past whitespace in the front of the source string if there is any.
    ///    This updates the position of the source string itself, so the caller
    ///    must save the position of the source string if it wishes to go back.
    while (isspace(**psrc)) { 
        (*psrc)++; 
    }
    
    /// 3. Put a Null Terminator where whitespace is found after the marked
    ///    string.
    for (code_len=0, cursor=*psrc; (code_len < code_max); code_len++, cursor++) {
        if (isspace(*cursor)) {
            *cursor = 0;
            cursor++;
            break;
        }
    }
    
    /// 4. Go past any whitespace after the cursor position, and update cursor.
    while (isspace(*cursor)) { 
        cursor++; 
    }
    
    /// 5. reduce the message limit counter given the bytes we've gone past.
    *search_limit -= (cursor - front);
    
    return cursor;
}



uint8_t* goto_eol(uint8_t* src) {
    uint8_t* end = src;
    
    while ((*end != 0) && (*end != '\n')) {
        end++;
    }
    
    return end;
}


#define INPUT_SANITIZE() do { \
    if ((src == NULL) || (dst == NULL)) {   \
        *inbytes = 0;                                       \
        return -1;                                          \
    }                                                       \
    {   uint8_t* eol    = goto_eol(src);                    \
        *inbytes        = (int)(eol-src);                   \
        *eol   = 0;                                         \
    }                                                       \
} while(0)

#define INPUT_SANITIZE_FLAG_EOS(IS_EOS) do { \
    if ((src == NULL) || (dst == NULL)) {   \
        *inbytes = 0;                                       \
        return -1;                                          \
    }                                                       \
    {   uint8_t* eol    = goto_eol(src);                    \
        *inbytes        = (int)(eol-src);                   \
        IS_EOS          = (bool)(*eol == 0);                \
        *eol   = 0;                                         \
    }                                                       \
} while(0)



/** Basic Commands
  * ------------------------------------------------------------------------
  */

int cmd_quit(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    raise(SIGINT);
    return 0;
}


extern cmdtab_t* otter_cmdtab;
int cmd_cmdlist(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int bytes_out;
    char cmdprint[1024];

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    bytes_out = cmdtab_list(otter_cmdtab, cmdprint, 1024);
    dterm_puts(dt, "Commands available:\n");
    dterm_puts(dt, cmdprint);
    
    return 0;
}







/** Environment Commands
  * ------------------------------------------------------------------------
  */

int cmd_set(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
///@todo This will do setting of Otter Env Variables.
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    fprintf(stderr, "\"set\" command not yet implemented\n");

    return 0;
}


int cmd_sethome(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
///@todo make this a recipient for "set HOME" or simply remove it.  Must be aligned
///      with environment variable module.

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    if (*inbytes >= 1023) {
        dterm_puts(dt, "Error: supplied home-path is too long, must be < 1023 chars.\n");
    }
    else {
        strcpy(home_path, (char*)src);
        if (home_path[*inbytes]  != '/') {
            home_path[*inbytes]   = '/';
            home_path[*inbytes+1] = 0;
        }
        home_path_len = *inbytes+1;
    }
    
    return 0;
}







/** User & Addressing Commands
  * ------------------------------------------------------------------------
  */

int cmd_chuser(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// src is a string containing:
/// - "guest" which is a single argument
/// - "user [address]"
/// - "admin [address" (alias for for "user")
/// - "root [address]"
/// 
/// * If address is not specified, the local address (0) is assumed.
/// * Address must be a value in BINTEX representing an integer.
/// * user/address combination must be added via useradd
///
#if (OTTER_FEATURE(SECURITY) != ENABLED)
    /// dt == NULL is the initialization case, but it is unused here.
    if (dt == NULL) {
        return 0;
    }
    INPUT_SANITIZE();
    dterm_puts(dt, "--> This build of "OTTER_PARAM_NAME" does not support security or users.\n");
    return 0;

#else 
    int test_id;
    uint8_t* cursor;
    
    /// dt == NULL is the initialization case.
    /// 
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    // The user search implementation is not optimized for speed, it just uses 
    // strcmp and strlen
    if (*inbytes == 0) {
        test_id = (int)USER_guest;
        goto cmd_chuser_END;
    }
    
    /// go past the first parameter.  
    /// - src will point to the front.
    /// - cursor will point to the parameter after the first one.
    cursor = sub_markstring(&src, inbytes, *inbytes);
    
    /// Check if parameter is a know usertype
    if (strcmp("guest", (const char*)src) == 0) {
        test_id = (int)USER_guest;
    }
    else if (strcmp("admin", (const char*)src) == 0) {
        test_id = (int)USER_user;
    }
    else if (strcmp("user", (const char*)src) == 0) {
        test_id = (int)USER_user;
    }
    else if (strcmp("root", (const char*)src) == 0) {
        test_id = (int)USER_root;
    }
    else {
        test_id = -1;
    }
    
    /// If user parameter was entered incorrectly, so print error and bail.
    /// If user parameter was entered as Admin/User or Root, then we need  
    /// to look for additional parameters.
    if ((test_id == -1) || (test_id > (int)USER_guest)) {
        dterm_puts(dt, (char*)src);
        dterm_puts(dt, " is not a recognized user type.\nTry: guest, admin, root\n");
        goto cmd_chuser_END;
    }
    
    ///@todo this section needs attention to fault tolerance.  Needs to be able to handle incorrect inputs and early termination.
    /// root or admin user type.
    /// - no additional parameters means to use local keys (-l)
    /// - flag parameter can be -l or -d: local or database user search
    /// - -l flag parameter is followed by AES key in bintex
    /// - -d flag parameter is followed by ID number in bintex
    if (test_id < (int)USER_guest) {
        char    mode;
        
        // get & test the flag parameter
        src = cursor;
        if (*src == '-') {
            src++;
            mode = (char)*src;
            src++;
            *inbytes -= 2;
        }
        else {
            mode = 'l';
        }
        
        // for l, look for a key parameter in BINTEX and pass it to user module
        // for d, look for ID parameter in BINTEX and pass it to user db module
        if ((mode != 'l') && (mode != 'd')) {
            dterm_puts(dt, "--> Option not supported.  Use -l or -d.\n");
        }
        else {
            if (mode == 'l') {
                size_t  key_size;
                uint8_t aes128_key[16];
                key_size = bintex_ss((unsigned char*)src, aes128_key, 16);

                if (key_size != 16) {
                    dterm_puts(dt, "--> Key is not a recognized size: should be 128 bits.\n");
                }
                else if (0 != user_set_local((USER_Type)test_id, 0, aes128_key)) {
                    dterm_puts(dt, "--> Key cannot be added to this user.\n");
                }
            }
            else {
#               if OTTER_FEATURE(OTDB)
                size_t  id_size;
                uint64_t id64 = 0;
                id_size = bintex_ss((unsigned char*)src, (uint8_t*)&id64, 8);
                if (0 != user_set_db((USER_Type)test_id, id64)) {
                    dterm_puts(dt, "--> User ID not found.\n");
                }
#               else
                dterm_puts(dt, "--> This build of otter does not support external DB user lookup.\n");
#               endif
            }
        }
    }
    
    /// guest usertype
    else {
        user_set_local(USER_guest, 0, NULL);
    }

    cmd_chuser_END:

    /// User or Root attempted accesses will require a transmission of an
    /// authentication command over the M2DEF protocol, via MPipe.  Attempted
    /// Guest access will not do authentication.
    return 0;
#endif
}



int cmd_su(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// su takes no arguments, and it wraps cmd_chuser
    int chuser_inbytes;
    char* chuser_cmd = "root";
    
    if (dt == NULL) {
        return 0;
    }
    INPUT_SANITIZE();

    return cmd_chuser(dt, dst, &chuser_inbytes, (uint8_t*)chuser_cmd, dstmax);
}



int cmd_useradd(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// su takes no arguments, and it wraps cmd_chuser
    int chuser_inbytes;
    char* chuser_cmd = "root";
    
    if (dt == NULL) {
        return 0;
    }
    INPUT_SANITIZE();

    return cmd_chuser(dt, dst, &chuser_inbytes, (uint8_t*)chuser_cmd, dstmax);
}



int cmd_whoami(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// whoami command does not send any data to the target, it just checks to see
/// who is the active CLI user, and if it has been authenticated successfully.
    char output[64];
    
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();     
    
    if (*inbytes != 0) {
        dterm_puts(dt, "Usage: whoami [no parameters]\n");
        dterm_puts(dt, "Indicates the current user and address\n");
    }
    else {
        if (user_idval_get() == 0) {
            sprintf(output, "%s@local", user_typestring_get());
        }
        else {
            sprintf(output, "%s@%016llX", user_typestring_get(), user_idval_get());
        }
        
        dterm_puts(dt, output);
        
        /// @todo indicate if authentication response has been successfully
        ///       received from the target.
        if (user_typeval_get() < USER_guest) {
            dterm_puts(dt, " [no auth available yet]");
        }
        
        dterm_putc(dt, '\n');
    }
    
    return 0;
}







/** Protocol I/O Commands
  * ------------------------------------------------------------------------
  */

///@todo make separate commands for file & string based input
// Raw Protocol Entry: This is implemented fully and it takes a Bintex
// expression as input, with no special keywords or arguments.
int cmd_raw(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    const char* filepath;
    FILE*       fp;
    int         bytesout;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    // Consider absolute path
    if (src[0] == '/') {
        filepath = (const char*)src;
    }
    
    // Build path from relative path, co-opting packet buffer temporarily
    else {
        int bytes_left;
        bytes_left = (HOME_PATH_MAX - home_path_len);
        strncat((char*)home_path, (char*)src, bytes_left);
        filepath = (const char*)home_path;
    }
    
    // Try opening the file.  If it doesn't work, then assume the input is a
    // bintex string and not a file string
    fp = fopen(filepath, "r");
    if (fp != NULL) {
        bytesout = bintex_fs(fp, (unsigned char*)dst, (int)dstmax);
        fclose(fp);
    }
    else {
        bytesout = bintex_ss((unsigned char*)src, (unsigned char*)dst, (int)dstmax);
    }
    
    // Undo whatever was done to the home_path
    home_path[home_path_len] = 0;
    
    ///@todo convert the character number into a line and character number
    if (bytesout < 0) {
        dterm_printf(dt, "Bintex error on character %d.\n", -bytesout);
    }
    else if (cliopt_isverbose() && (bytesout > 0)) {
        fprintf(stdout, "--> raw packetizing %d bytes (max=%zu)\n", bytesout, dstmax);
    }

    return bytesout;
}






int cmdext_hbuilder(void* hb_handle, void* cmd_handle, dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    bool is_eos     = false;
    size_t bytesout = 0;
    int rc;
    
    INPUT_SANITIZE_FLAG_EOS(is_eos);
    
    //fprintf(stderr, "hbuilder invoked: %.*s\n", *inbytes, (char*)src);
    
#   if OTTER_FEATURE(HBUILDER)
    rc = hbuilder_runcmd(hb_handle, cmd_handle, dst, &bytesout, dstmax, src, inbytes);
    
    if (rc < 0) {
        dterm_printf(dt, "HBuilder Command Error (code %d)\n", rc);
        if (rc == -2) {
            dterm_printf(dt, "--> Input Error on character %zu\n", bytesout);
        }
    }
    else if ((rc > 0) && cliopt_isverbose()) {
        fprintf(stdout, "--> HBuilder packetizing %zu bytes\n", bytesout);
    }

#   else
    fprintf(stderr, "hbuilder invoked, but not supported.\n--> %s\n", src);
    rc = -2;

#   endif
    
    return rc;
}




/*
int cmd_hbcc(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// HBCC src must be a code-word, then whitespace, then optionally a bintex string.
/// @todo wrap this into command handling library
    bool is_eos = false;
    int output_code;
    size_t bytesout = 0;
    
    /// Initialization
    if (dt == NULL) {
#       if defined(__HBUILDER__)
        hbcc_init();
#       endif
        return 0;
    }
    
    INPUT_SANITIZE_FLAG_EOS(is_eos);
    
#   if defined(__HBUILDER__)
    {   //size_t      code_len;
        //size_t      code_max;
        uint8_t*    cursor;
    
        /// 1. This routine will isolate the command or argument string within
        ///    the input string.  It updates the the position of src to remove
        ///    preceding whitespace, and returns the position after the command
        ///    or argument string.
        cursor = sub_markstring(&src, inbytes, 32);
        
        /// 2. The code word might be an argument preceded by a hyphen ('-').
        ///    This is of the typical format for unix command line apps.
        ///@todo this functionality is not used exactly at this moment.
        if (src[0] == '-') {
            // Process parameter list.  Currently undefined
            //mode = 1;
            output_code = 0;
        }
        
        /// 3. If not an argument, the command is an API command (not a control)
        ///    and thus has the normal codeword+bintex format.
        
        else {
            uint8_t temp_buffer[128];
            bytesout = bintex_ss((unsigned char*)cursor, (unsigned char*)temp_buffer, (int)sizeof(temp_buffer));
            
#           if (defined(__PRINT_BINTEX))
            test_dumpbytes(temp_buffer, bytesout, "HBCC Partial Bintex output");
#           endif
            
            /// hbcc_generate() will create the complete, ALP-framed API message.
            /// - It will return a negative number on error
            /// - It returns 0 when command is queued
            /// - It returns positive number when commands are queued and should
            ///   be packetized over MPipe.
            /// - Possible to call 'hbcc flush' anytime to page-out commands.
            output_code = hbcc_generate(dst, &bytesout, dstmax, (const char*)src, (size_t)bytesout, temp_buffer);
            
            //if ((output_code == 0) && is_eos) {
            //    bytesout = hbcc_flush();
            //}
            //fprintf(stderr, "hbcc called, generated %d bytes\n", bytesout);
        }

        if (cliopt_isverbose() && (output_code > 0)) {
            fprintf(stdout, "--> hbcc packetizing %zu bytes\n", bytesout);
        }

        return (int)output_code;
    }
    
#   else
    fprintf(stderr, "hbcc invoked %s\n", src);
    return -2;
    
#   endif
}
*/





///@todo these commands are simply for test purposes right now. 

// ID = 0
int app_null(dterm_t* dt, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dt == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    fprintf(stderr, "null invoked %s\n", src);
    return -1;
}


