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
#include "cmd_api.h"
#include "cmdutils.h"
#include "cliopt.h"
#include "dterm.h"
#include "devtable.h"
#include "otter_cfg.h"
#include "subscribers.h"

// HBuilder Libraries
#include <argtable3.h>
#include <bintex.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <errno.h>






int cmd_xnode(dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    static const char xnode_name[] = "xnode";
    struct arg_str* nodeuser= arg_str1(NULL,NULL,"user",        "User type");
    struct arg_str* nodeuid = arg_str1(NULL,NULL,"uid",         "UID of device in bintex");
    struct arg_str* nodecmd = arg_str1(NULL,NULL,"node cmd",    "Command put in quotes (\"\") to run");
    struct arg_end* end     = arg_end(5);
    void* argtable[]        = { nodeuser, nodeuid, nodecmd, end };
    int argc;
    char** argv;
    const cmdtab_item_t* cmdptr;
    int bytesin = 0;
    int rc;
    USER_Type usertype;
    uint64_t uid_val = 0;
    devtab_node_t node = NULL;
    
    /// dt == NULL is the initialization case.
    /// There may not be an initialization for all command groups.
    if (dth == NULL) {
        return 0;
    }
    
    INPUT_SANITIZE();
    
    argc = cmdutils_parsestring(&argv, xnode_name, (char*)src, (char*)src, (size_t)*inbytes);

    rc = cmdutils_argcheck(argtable, end, argc, argv);
    if (rc != 0) {
        rc = -1;
        goto cmd_xnode_FREEARGS;
    }
    
    usertype = user_get_type(nodeuser->sval[0]);
    
    bintex_ss(nodeuid->sval[0], (uint8_t*)&uid_val, 8);
    node = devtab_select(dth->endpoint.devtab, uid_val);
    if (node == NULL) {
        rc = -2;
        goto cmd_xnode_FREEARGS;
    }
    
    /// Make sure node has the necessary keys
    ///@todo this code block is used in multiple places
    usertype = user_get_type(nodeuser->sval[0]);
    if (devtab_validate_usertype(node, usertype) != 0) {
        rc = -4;
        goto cmd_xnode_FREEALL;
    }
    dth->endpoint.usertype  = usertype;
    dth->endpoint.node      = node;
    
    cmdptr  = cmd_quoteline_resolve((char*)nodecmd->sval[0], dth);
    rc      = cmd_run(cmdptr, dth, dst, &bytesin, (uint8_t*)nodecmd->sval[0], dstmax);

    cmd_xnode_FREEALL:
    /// Free argtable & argv now that they are not necessary
    //free(xnode_cmd);

    cmd_xnode_FREEARGS:
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    cmdutils_freeargv(argv);

    return rc;
}



