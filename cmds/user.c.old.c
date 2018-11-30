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
#include "cmdutils.h"
#include "envvar.h"

#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_cfg.h"
//#include "test.h"


#include "user.h"


// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>



/// This key is hardcoded for AES128
///@todo have a way to make this dynamic based on cli parameters, or mode params
static uint8_t user_key[16];






int cmd_chuser(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
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
    if (dth == NULL) {
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
    if (dth == NULL) {
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
        dterm_puts(dth->dt, (char*)src);
        dterm_puts(dth->dt, " is not a recognized user type.\nTry: guest, admin, root\n");
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
            dterm_puts(dth->dt, "--> Option not supported.  Use -l or -d.\n");
        }
        else {
            if (mode == 'l') {
                size_t  key_size;
                uint8_t aes128_key[16];
                key_size = bintex_ss((unsigned char*)src, aes128_key, 16);

                if (key_size != 16) {
                    dterm_puts(dth->dt, "--> Key is not a recognized size: should be 128 bits.\n");
                }
                else if (0 != user_set_local((USER_Type)test_id, 0, aes128_key)) {
                    dterm_puts(dth->dt, "--> Key cannot be added to this user.\n");
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
                dterm_puts(dth->dt, "--> This build of otter does not support external DB user lookup.\n");
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



int cmd_su(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// su takes no arguments, and it wraps cmd_chuser
    int chuser_inbytes;
    char* chuser_cmd = "root";
    
    if (dth->dt == NULL) {
        return 0;
    }
    INPUT_SANITIZE();

    return cmd_chuser(dth, dst, &chuser_inbytes, (uint8_t*)chuser_cmd, dstmax);
}



int cmd_useradd(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// su takes no arguments, and it wraps cmd_chuser
    int chuser_inbytes;
    char* chuser_cmd = "root";
    
    if (dth == NULL) {
        return 0;
    }
    INPUT_SANITIZE();

    return cmd_chuser(dth, dst, &chuser_inbytes, (uint8_t*)chuser_cmd, dstmax);
}



int cmd_whoami(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// whoami command does not send any data to the target, it just checks to see
/// who is the active CLI user, and if it has been authenticated successfully.
    char output[64];
    
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    if (*inbytes != 0) {
        dterm_puts(dth->dt, "Usage: whoami [no parameters]\n");
        dterm_puts(dth->dt, "Indicates the current user and address\n");
    }
    else {
        if (user_idval_get() == 0) {
            sprintf(output, "%s@local", user_typestring_get());
        }
        else {
            sprintf(output, "%s@%016llX", user_typestring_get(), user_idval_get());
        }
        
        dterm_puts(dth->dt, output);
        
        /// @todo indicate if authentication response has been successfully
        ///       received from the target.
        if (user_typeval_get() < USER_guest) {
            dterm_puts(dth->dt, " [no auth available yet]");
        }
        
        dterm_putc(dth->dt, '\n');
    }
    
    return 0;
}

