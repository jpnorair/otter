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
#include "devtable.h"

#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_cfg.h"
//#include "test.h"

#include <argtable3.h>
#include <bintex.h>

#include "user.h"


// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>



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
    char** argv;
    int argc;
    int rc;
    
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(&argv, "chuser", (char*)src, (char*)src, (size_t)*inbytes);
    if (argc != 0) {
        rc = -256 + argc;
    }
    else {
        struct arg_str* utype   = arg_str1(NULL,NULL,"usertype",    "root|user|guest");
        struct arg_str* uid     = arg_str0(NULL,NULL,"UID",         "64 bit UID as Bintex expression");
        struct arg_int* vid     = arg_int0("v","vid","VID",         "VID as integer, 0-65535.");
        struct arg_end* end     = arg_end(4);
        void* argtable[]        = { utype, uid, vid, end };
        devtab_node_t node;
        devtab_endpoint_t* endpoint;
        USER_Type usertype;
        
        ///@todo wrap this routine into cmdutils subroutine
        if (arg_nullcheck(argtable) != 0) {
            rc = -1;
            goto cmd_chuser_TERM;
        }
        if ((argc <= 1) || (arg_parse(argc, argv, argtable) > 0)) {
            arg_print_errors(stderr, end, argv[0]);
            rc = -2;
            goto cmd_chuser_TERM;
        }
        
        /// UID and VID are optional.  If UID is present, it takes precedence.
        /// If neither are present, the implicit address 0 is used.
        if (uid->count > 0) {
            uint64_t uidval = 0;
            bintex_ss(uid->sval[0], (uint8_t*)&uidval, 8);
            node = devtab_select(dth->endpoint.devtab, uidval);
        }
        else if (vid->count > 0) {
            node = devtab_select_vid(dth->endpoint.devtab, (uint16_t)vid->ival[0]);
        }
        else {
            node = devtab_select(dth->endpoint.devtab, 0);
        }
        
        /// Make sure a node is found
        if (node == NULL) {
            rc = -3;
            goto cmd_chuser_TERM;
        }
        
        /// Make sure node has the necessary keys
        endpoint = devtab_resolve_endpoint(node);
        if (endpoint == NULL) {
            rc = -4;
            goto cmd_chuser_TERM;
        }
        
        /// User Type is a Mandatory Field.
        if ((strcmp("admin", utype->sval[0]) == 0)
        || (strcmp("user", utype->sval[0]) == 0)) {
            usertype = USER_user;
            if (endpoint->userctx == NULL) {
                rc = -5;
                goto cmd_chuser_TERM;
            }
        }
        else if (strcmp("root", utype->sval[0]) == 0) {
            usertype = USER_root;
            if (endpoint->rootctx == NULL) {
                rc = -6;
                goto cmd_chuser_TERM;
            }
        }
        else {
            usertype = USER_guest;
        }
        
        rc = 0;
        dth->endpoint.usertype  = usertype;
        dth->endpoint.node      = node;
        
        cmd_chuser_TERM:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    }
    
    cmdutils_freeargv(argv);
    
    return rc;
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







int cmd_whoami(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
/// whoami command does not send any data to the target, it just checks to see
/// who is the active CLI user, and if it has been authenticated successfully.
    int rc = 0;
    
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
    else if (dth->endpoint.node == NULL) {
        rc = -1;
        goto cmd_whoami_END;
    }
    else {
        char output[80];
        char* cursor;
        devtab_endpoint_t* endpoint;
        
        endpoint = devtab_resolve_endpoint(dth->endpoint.node);
        if (endpoint == NULL) {
            rc = -2;
            goto cmd_whoami_END;
        }
        
        cursor = output;
        
        switch (dth->endpoint.usertype) {
            case USER_root: cursor = stpcpy(cursor, "root@");   break;
            case USER_user: cursor = stpcpy(cursor, "user@");   break;
            default:        cursor = stpcpy(cursor, "guest@");  break;
        }
        
        if (endpoint->uid == 0) {
            cursor = stpcpy(cursor, "local\n");
        }
        else {
            cursor += cmdutils_uint8_to_hexstr(cursor, (uint8_t*)&endpoint->uid, 8);
            cursor += sprintf(cursor, " (vid=%i)\n", endpoint->vid);
        }
        
        dterm_puts(dth->dt, output);
    }
    
    cmd_whoami_END:
    return rc;
}

