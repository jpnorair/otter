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
#include "cmdsearch.h"


#include <string.h>
#include <stdio.h>
#include <ctype.h>





/// Binary Search Table for Commands

// sorted list of supported commands

///@todo this will need to be dynamic and build during startup based on an 
///      initialization file and plug-in libraries stored with the app.
///
static const cmd_t commands[CMD_COUNT] = {
    { "asapi",      &app_asapi,     NULL, NULL },
    { "bye",        &cmd_quit,      NULL, NULL },
    { "confit",     &app_confit,    NULL, NULL },
    { "dforth",     &app_dforth,    NULL, NULL },
    { "file",       &app_file,      NULL, NULL },
    { "hbcc",       &cmd_hbcc,      NULL, NULL },
    { "log",        &app_log,       NULL, NULL },
    { "null",       &app_null,      NULL, NULL },
    { "quit",       &cmd_quit,      NULL, NULL },
    { "raw",        &cmd_raw,       NULL, NULL },
    { "sec",        &app_sec,       NULL, NULL },
    { "sensor",     &app_sensor,    NULL, NULL },
    { "sethome",    &cmd_sethome,   NULL, NULL },
    { "su",         &cmd_su,        NULL, NULL },
    { "whoami",     &cmd_whoami,    NULL, NULL },
};












// comapres two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
int local_strcmp(char *s1, char *s2);


// comapres first x characters of two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
int local_strcmpc(char *s1, char *s2, int x);







int cmdsearch_init(cmd_t* init_table) {
    
    ///@todo loading a cmd_table from memory
    if (init_table != NULL) {
        
    }
    
    return 0;
}










///@todo Consider refactoring: this function is ugly, slow, and most likely
///      un-necessary in its present state
//int cmd_parse(char* cmdname, char *cmdstr) {
//	int i = 0;
//    
//	while( (i < CMD_NAMESIZE) && (cmdstr[i] != 0) && (cmdstr[i] != ' ') ) {
//		cmdname[i] = cmdstr[i];
//        i++;
//    }
//    cmdname[i] = 0;
//
//    return (i > 0) ? i+1 : -1;
//}


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
    
    // Go past spaces after the command, being mindful of condition with no
    // command parameters
//    while (*cmdline != 0) {
//        if (*cmdline != ' ') {
//            break;
//        }
//        cmdline++;
//        diff++;
//    }
    
    // Return position of parameters, or length of command if no parameters.
//    return (int)diff;
}



///@todo Probably a good idea to make the command list a ternary search tree,
///      or at least a binary tree that can be added-to






const cmd_t* cmd_search(char *cmdname) {
/// Verify that cmdname is not a zero-length string, then search for it in the
/// list of available commands
    
    if (*cmdname != 0) {
    
        // This is a binary search across the static array
        int l           = 0;
        int r           = CMD_COUNT - 1;
        int cci;
        int csc;
    
        while (r >= l) {
            cci = (l + r) >> 1;
            csc = local_strcmp((char*)commands[cci].name, cmdname);
            
            switch (csc) {
                case -1: r = cci - 1; break;
                case  1: l = cci + 1; break;
                default: return &commands[cci];
            }
        }
        // End of binary search implementation
        
    }
    
	return NULL;
}



const cmd_t* cmd_subsearch(char *namepart) {

    if (*namepart != 0) {
        int len = (int)strlen(namepart);

        // try to find single match
        int l   = 0;
        int r   = CMD_COUNT - 1;
        int lr  = -1;
        int rr  = -1;
        int cci;
        int csc;
        
        while(r >= l) {
            cci = (l + r) >> 1;
            csc = local_strcmpc((char*)commands[cci].name, namepart, len);
            
            switch (csc) {
                case -1: r = cci - 1; break;
                case  1: l = cci + 1; break;
                    
                // check for matches left and right
                default:
                    if (cci > 0) {
                        lr = local_strcmpc((char*)commands[cci - 1].name, namepart, len);
                    }
                    if (cci < (CMD_COUNT - 1)) {
                        rr = local_strcmpc((char*)commands[cci + 1].name, namepart, len);
                    }
                    return (lr & rr) ? &commands[cci] : NULL;
            }
        }
    
    }
        
	return NULL;
}






int local_strcmp(char *s1, char *s2) {
	for (; (*s1 == *s2) && (*s1 != 0); s1++, s2++);	
	return (*s1 < *s2) - (*s1 > *s2);
}


int local_strcmpc(char *s1, char *s2, int x) {
    for (int i = 0; (*s1 == *s2) && (*s1 != 0) && (i < x - 1); s1++, s2++, i++) ;
    return (*s1 < *s2) - (*s1 > *s2);
}
