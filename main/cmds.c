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
#include "cmds.h"
#include "dterm.h"

// Local Headers/Libraries
#include "bintex.h"

// Standard C & POSIX Libraries
#include <signal.h>
#include <stdio.h>
#include <string.h>




/// Variables used across shell commands
///@todo put these into some form of data structure, potentially the "cli" 
///      datastructure.
int user_id                     = 2;            // Guest

const char user_str_guest[]     = "guest";
const char user_str_admin[]     = "admin";
const char user_str_root[]      = "root";
const char user_prompt_guest[]  = "~";
const char user_prompt_admin[]  = "$";
const char user_prompt_root[]   = "#";

const char *user_str            = user_str_guest;
const char *user_str_lookup[3]  = { user_str_root, user_str_admin, user_str_guest };

const char *user_prompt         = user_prompt_guest;
const char *user_prompt_lookup[3] = { user_prompt_root, user_prompt_admin, user_prompt_guest };

#define HOME_PATH_MAX           1024
char home_path[HOME_PATH_MAX]   = "~/";
int home_path_len               = 2;


/// This key is hardcoded for AES128
///@todo have a way to make this dynamic based on cli parameters, or mode params
uint8_t user_key[16];







int cmd_quit(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    raise(SIGQUIT);
    return 0;
}



int cmd_sethome(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    unsigned int srclen;
    srclen = (unsigned int)strlen((char*)src);
    
    if (srclen >= 1023) {
        dterm_puts(dt, "Error: supplied home-path is too long, must be < 1023 chars.\n");
        return 0;
    }
    
    strcpy(home_path, (char*)src);
    if (home_path[srclen]  != '/') {
        home_path[srclen]   = '/';
        home_path[++srclen] = 0;
    }
    home_path_len = srclen;
    
    // No bytes out, just stores into home_path
    return 0;
}




int cmd_su(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
/// src is a string containing the user to switch-to, which may be:
/// - "guest" or [empty]
/// - "user" or "admin"
/// - "root"
    int test_id     = -1;
    int bytes_out   = 0;

    
    // The user search implementation is not optimized for speed, it just uses 
    // strcmp and strlen
    if (strlen((const char*)src) == 0) {
        test_id = 2;
    }
    else if (strcmp("guest", (const char*)src) == 0) {
        test_id = 2;
    }
    else if (strcmp("admin", (const char*)src) == 0) {
        test_id = 1;
    }
    else if (strcmp("user", (const char*)src) == 0) {
        test_id = 1;
    }
    else if (strcmp("root", (const char*)src) == 0) {
        test_id = 0;
    }
    
    /// If user parameter was entered incorrectly, so print error and bail
    if ((test_id == -1) || (test_id > 2)) {
        dterm_puts(dt, (char*)src);
        dterm_puts(dt, " is not a recognized user.\nTry: guest, admin, root\n");
        return 0;
    }
    
    /// If user parameter was entered as Admin/User or Root it requires entry
    /// of a hex AES128 key for that user.  It will be authenticated on the 
    /// target, as well, via the M2DEF Auth protocol. 
    if (test_id < 2) {
        int     i;
        char    aes128_key[40];
        char*   key_ptr;
        
        dterm_puts(dt, "Key [AES128]: ");
        
        // Read in the 16-byte hex key for AES128
        
        ///@todo Implement a readline function for dterm
        dterm_scanf(dt, "%32s", aes128_key);
        
        key_ptr = aes128_key;
        for (i=0; i<16; i++) {
            user_key[i] = 0;
        
            if (*key_ptr != 0) {
                user_key[i] = (*key_ptr++);
                
                key_ptr++;
            }
        }
        
        
        /*
        while ((keychars = read(dt->fd_in, &char_in, 1)) > 0) {
            int hexval = -1;
                
            if (char_in == '\n') {
                int i           = (hex_chars + 1) >> 1;
                aes128_key[i] <<= 4;
                break;
            }
            else if ((char_in >= '0') && (char_in <= '9')) {
                hexval = ((int)char_in - '0');
            }
            else if ((char_in <= 'a') && (char_in <= 'f')) {
                hexval = ((int)char_in - 'a' + 10);
            }
            else if ((char_in <= 'A') && (char_in <= 'F')) {
                hexval = ((int)char_in - 'a' + 10);
            }
                
            if (hexval >= 0) {
                int i;
                dterm_putc(dt, char_in);
                    
                i               = hex_chars >> 1;
                aes128_key[i] <<= 4;
                aes128_key[i]  |= hexval;
                    
                if (++hex_chars == 32) {
                    break;
                }
            }
        }
        dterm_putc(dt, '\n');
        */
        
        /// Load aes128 key into actual user_key buffer.
        ///@todo store this in CLI object
        memcpy((uint8_t*)user_key, aes128_key, 16);
        
        ///@todo build the protocol command for authenticating the user.
        bytes_out = 0;
    }
    
    
    /// Save User Parameters.
    /// Note that these might not properly authenticate with the target, in
    /// which case an Auth Response from the target should cause the user to 
    /// be dropped back into guest on failure.
    user_id     = test_id;
    user_str    = user_str_lookup[test_id];
    user_prompt = user_prompt_lookup[test_id];
    
    
    /// User or Root attempted accesses will require a transmission of an
    /// authentication command over the M2DEF protocol, via MPipe.  Attempted
    /// Guest access will not do authentication.
    return bytes_out;
}



int cmd_whoami(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
/// whoami command does not send any data to the target, it just checks to see
/// who is the active CLI user, and if it has been authenticated successfully.
    
    if (strlen((char*)src) != 0) {
        dterm_puts(dt, "Usage: whoami [no parameters]\n");
        dterm_puts(dt, "Indicates the current user, and if it has been authenticated on the target.\n");
    }
    else {
        dterm_puts(dt, (char*)user_str);
        
        /// @todo indicate if authentication response has been successfully
        ///       received from the target.
        if (user_id < 2) {
            dterm_puts(dt, " [no auth available yet]");
        }
        
        dterm_putc(dt, '\n');
    }
    
    return 0;
}



///@todo make separate commands for file & string based input
// Raw Protocol Entry: This is implemented fully and it takes a Bintex
// expression as input, with no special keywords or arguments.
int cmd_raw(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    const char* filepath;
    FILE*       fp;
    int         bytesout;
    
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
        bytesout = -bytesout;
        dterm_printf(dt, "Bintex error on character %d.\n", bytesout);
        return 0;
    }

    return bytesout;
}






///@todo these commands are simply for test purposes right now. 


// ID = 0
int app_null(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "null invoked %s\n", src);
    return -1;
}


// ID = 1
int app_file(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "file invoked %s\n", src);
    return -1;
}


// ID = 2
int app_sensor(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "sensor invoked %s\n", src);
    return -1;
}


// ID = 3
int app_sec(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "sec invoked %s\n", src);
    return -1;
}


// ID = 4
int app_log(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "logger invoked %s\n", src);
    return -1;
}


// ID = 5
int app_dforth(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "dforth invoked %s\n", src);
    return -1;
}


// ID = 6
int app_confit(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "confit invoked %s\n", src);
    return -1;
}


// ID = 7
int app_asapi(dterm_t* dt, uint8_t* dst, uint8_t* src, size_t dstmax) {
    fprintf(stderr, "asapi invoked %s\n", src);
    return -1;
}


