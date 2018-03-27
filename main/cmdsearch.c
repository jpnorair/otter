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

// cmdtab library header
#include <cmdtab/cmdtab.h>

// Local Headers
#include "cmds.h"
#include "cmdsearch.h"


#include <string.h>
#include <stdio.h>
#include <ctype.h>





/// Binary Search Table for Commands

// sorted list of supported commands

///@todo this will need to be dynamic and build during startup based on an 
///      initialization file and plug-in libraries stored with the app.
///
typedef struct {
    const char      name[8]; 
    cmdaction_t     action; 
} cmd_t;

static const cmd_t otter_commands[] = {
    { "bye",        &cmd_quit  },
   /* { "file",       &app_file }, */
   /* { "hbcc",       &cmd_hbcc }, */
    { "log",        &app_log },
    { "null",       &app_null },
    { "quit",       &cmd_quit },
    { "raw",        &cmd_raw },
    { "set",        &cmd_set },
    { "sethome",    &cmd_sethome },
    { "su",         &cmd_su },
    { "whoami",     &cmd_whoami },
};


///@todo Make this thread safe by adding a mutex here.
///      It's not technically required yet becaus only one thread in otter uses
///      cmdsearch, but we should put it in soon, just in case.
static cmdtab_t cmdtab_default;
static cmdtab_t* otter_cmdtab;




int cmd_init(cmdtab_t* init_table) {
    
    otter_cmdtab = (init_table == NULL) ? &cmdtab_default : init_table;

    /// Add Otter commands to the cmdtab.
    for (int i=0; i<(sizeof(otter_commands)/sizeof(cmd_t)); i++) {
        int rc;
        rc = cmdtab_add(otter_cmdtab, otter_commands[i].name, (void*)otter_commands[i].action, NULL);
        
        if (rc != 0) {
            fprintf(stderr, "ERROR: cmdtab_add() from %s line %d returned %d.\n", __FUNCTION__, __LINE__, rc);
            return -1;
        }
        
        /// Run command with dt=NULL to initialize the command
        ///@note This is specific to otter, it's not a requirement of cmdtab
        otter_commands[i].action(NULL, NULL, NULL, NULL, 0);
    }
    

    /// If HBuilder is enabled, pass this table into the hbuilder initializer
#   if OTTER_FEATURE(HBUILDER)
    
#   endif
    
    /// Any future command lists can be added below
    
    
    return 0;
}






int cmd_getname(char* cmdname, char* cmdline, size_t max_cmdname) {
	size_t diff = max_cmdname;
    
    // Copy command into cmdname, stopping when whitespace is detected, or
    // the command line (string) is ended.
    while ((diff != 0) && (*cmdline != 0) && !isspace(*cmdline)) {
        diff--;
        *cmdname++ = *cmdline++;
    }
    
    // Add command string terminator & determine command string length (diff)
    *cmdname    = 0;
    diff        = max_cmdname - diff;
    return (int)diff;    
}




const cmdtab_item_t* cmd_search(char *cmdname) {
    return cmdtab_search(otter_cmdtab, cmdname);
}



const cmdtab_item_t* cmd_subsearch(char *namepart) {
    return cmdtab_subsearch(otter_cmdtab, namepart);
}



