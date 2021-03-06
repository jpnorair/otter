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
#include "cmdutils.h"

#include "devtable.h"

#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_app.h"
#include "otter_cfg.h"
//#include "test.h"

#include <argtable3.h>
#include <bintex.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


static int sub_editnode(dterm_handle_t* dth, int argc, char** argv, bool require_exists) {
    struct arg_str* uid     = arg_str1(NULL,NULL,"UID",             "64 bit UID as Bintex expression");
    struct arg_int* vid     = arg_int0("v","vid","VID",             "VID as integer, 0-65535.");
    struct arg_lit* pre     = arg_lit0("p", NULL,                   "If UID pre-exists, edit the node.");
    struct arg_str* intf    = arg_str0("i", "intf", "ttyfile",      "TTY associated with node.");
    struct arg_str* rootkey = arg_str0("r", "root", "key",          "128 bit AES key as Bintex expression");
    struct arg_str* userkey = arg_str0("u", "user", "key",          "128 bit AES key as Bintex expression");
    struct arg_end* end     = arg_end(10);
    void* argtable[]        = { uid, vid, intf, rootkey, userkey, end };
    uint16_t vid_val;
    uint64_t uid_val = 0;
    void* intf_val;
    uint8_t* rootkey_val    = NULL;
    uint8_t* userkey_val    = NULL;
    uint8_t rootkey_dat[16];
    uint8_t userkey_dat[16];
    int rc = 0;
    devtab_node_t node = NULL;
    otter_app_t* appdata = dth->ext;

    ///@todo wrap this routine into cmdutils subroutine
    if (arg_nullcheck(argtable) != 0) {
        rc = -1;
        goto sub_editnode_TERM;
    }

    if ((argc <= 1) || (arg_parse(argc, argv, argtable) > 0)) {
        arg_print_errors(stderr, end, argv[0]);
        rc = -2;
        goto sub_editnode_TERM;
    }

    /// UID is a required element
    bintex_ss(uid->sval[0], (uint8_t*)&uid_val, 8); 
    node = devtab_select(appdata->endpoint.devtab, uid_val);
    if( (require_exists && (node == NULL))
    || ((require_exists==false) && (pre->count==0) && (node != NULL))
    ) {
        rc = -3;
        goto sub_editnode_TERM;
    }

    if (vid->count > 0) {
        vid_val = (uint16_t)vid->ival[0];
    }
    else if (node != NULL) {
        vid_val = devtab_get_vid(appdata->endpoint.devtab, node);
    }
    else {
        vid_val = 0;
    }

    if (intf->count > 0) {
        ///@todo Must be able to do mpipe_intf_get(mph, ttyname)
        intf_val = mpipe_intf_fromfile(appdata->mpipe, intf->sval[0]);
    }
    else if (node != NULL) {
        intf_val = devtab_get_intf(appdata->endpoint.devtab, node);
    }
    else {
        intf_val = mpipe_intf_get(appdata->mpipe, 0);
    }

    if (rootkey->count > 0) {
        rootkey_val = rootkey_dat;
        memset(rootkey_dat, 0, 16);
        bintex_ss(rootkey->sval[0], rootkey_dat, 16);
    }
    else if (node != NULL) {
        rootkey_val = devtab_get_rootctx(appdata->endpoint.devtab, node);
    }
    else {
        rootkey_val = NULL;
    }

    if (userkey->count > 0) {
        userkey_val = userkey_dat;
        memset(userkey_dat, 0, 16);
        bintex_ss(userkey->sval[0], userkey_dat, 16);
    }
    else if (node != NULL) {
        userkey_val = devtab_get_userctx(appdata->endpoint.devtab, node);
    }
    else {
        userkey_val = NULL;
    }

    rc = devtab_insert(appdata->endpoint.devtab, uid_val, vid_val, intf_val, rootkey_val, userkey_val);

    sub_editnode_TERM:
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));

    return rc;
}



int cmd_mknode(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
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

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(dth->tctx, &argv, "mknode", (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
    }
    else {
        rc = sub_editnode(dth, argc, argv, false);
    }
    
    ///@todo better error reporting
    // ACK (0), NACK (!0)
    rc = (rc >= 0) ? 0 : rc;
    
    cmdutils_freeargv(dth->tctx, argv);
    return rc;
}


int cmd_chnode(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    char** argv;
    int argc;
    int rc;

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(dth->tctx, &argv, "chnode", (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
    }
    else {
        rc = sub_editnode(dth, argc, argv, true);
    }
    
    ///@todo better error reporting
    // ACK (0), NACK (!0)
    rc = (rc >= 0) ? 0 : rc;
    
    cmdutils_freeargv(dth->tctx, argv);
    return 0;
}


int cmd_rmnode(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    char** argv;
    int argc;
    int rc;
    otter_app_t* appdata;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    appdata = dth->ext;
    
    argc = cmdutils_parsestring(dth->tctx, &argv, "rmnode", (char*)src, (size_t)*inbytes);
    if (argc <= 0) {
        rc = -256 + argc;
    }
    else {
        struct arg_str* uid = arg_str1(NULL,NULL,"UID", "64 bit UID as Bintex expression");
        struct arg_end* end = arg_end(3);
        void* argtable[]    = { uid, end };
        uint64_t uid_val    = 0;
    
        ///@todo wrap this routine into cmdutils subroutine
        if (arg_nullcheck(argtable) != 0) {
            rc = -1;
            goto cmd_rmnode_TERM;
        }
        if ((argc <= 1) || (arg_parse(argc, argv, argtable) > 0)) {
            arg_print_errors(stderr, end, argv[0]);
            rc = -2;
            goto cmd_rmnode_TERM;
        }
        
        /// UID is a required element
        bintex_ss(uid->sval[0], (uint8_t*)&uid_val, 8);
        rc = devtab_remove(appdata->endpoint.devtab, uid_val);

        cmd_rmnode_TERM:
        arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
        cmdutils_freeargv(dth->tctx, argv);
    }
    
    ///@todo better error reporting
    // ACK (0), NACK (!0)
    rc = (rc >= 0) ? 0 : rc;
    
    return rc;
}


int cmd_lsnode(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int bytesout;
    char printbuf[1024];
    otter_app_t* appdata;

    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    appdata  = dth->ext;
    bytesout = devtab_list(appdata->endpoint.devtab, printbuf, 1024);
    
    dterm_send_cmdmsg(dth, "lsnode", printbuf);
    
    return 0;
}

