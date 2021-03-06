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
#include "otter_app.h"

// HB libraries
#include <cmdtab.h>
#include <bintex.h>

// Standard libs
#include <string.h>
#include <stdio.h>
#include <ctype.h>




/// Binary Search Table for Commands

// sorted list of supported commands

///@todo this will need to be dynamic and build during startup based on an 
///      initialization file and plug-in libraries stored with the app.
///
typedef struct {
    const char      name[12]; 
    cmdaction_t     action; 
} cmd_t;






static const cmd_t otter_commands[] = {
    { "bye",        &cmd_quit  },
    { "chnode",     &cmd_chnode },
    { "chuser",     &cmd_chuser },
    { "cmdls",      &cmd_cmdlist },
    { "file",       &cmd_fdp },
    { "lsnode",     &cmd_lsnode },
    { "mknode",     &cmd_mknode },
    { "null",       &app_null },
    { "quit",       &cmd_quit },
    { "raw",        &cmd_raw },
    { "rmnode",     &cmd_rmnode },
    { "sendhex",    &cmd_sendhex },
    { "su",         &cmd_su },
    { "var",        &cmd_var },
    { "whoami",     &cmd_whoami },
    { "xloop",      &cmd_xloop },
    { "xnode",      &cmd_xnode },
};




typedef enum {
    EXTCMD_null     = 0,
    EXTCMD_path,
    EXTCMD_MAX
} otter_extcmd_t;




int cmd_init(cmdtab_t** init_table, const char* xpath) {
    
    if (init_table == NULL) {
        return -1;
    }

    if (*init_table == NULL) {
        *init_table = malloc(sizeof(cmdtab_t));
        if (*init_table == NULL) {
            return -2;
        }
        if (cmdtab_init(*init_table) != 0) {
            return -3;
        }
    }

    /// cmdtab_add prioritizes subsequent command adds, so the highest priority
    /// commands should be added last.

    /// First, add commands that are available from the external command path.
    if (xpath != NULL) {
        size_t xpath_len = strlen(xpath);
        char buffer[256];
        char* cmd;
        FILE* stream;
        int test;
        if (xpath_len > 0) { 
            ///@todo make this find call work properly on mac and linux.
#           if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
            snprintf(buffer, 256, "find %s -perm +111 -type f", xpath);
            stream = popen(buffer, "r");
#           elif defined(__linux__)
            snprintf(buffer, 256, "find %s -perm /u=x,g=x,o=x -type f", xpath);
            stream = popen(buffer, "r");
#           else
            stream = NULL;
#           endif
            if (stream != NULL) {
                do {
                    test = fscanf(stream, "%s", buffer);
                    if (test == 1) {
                        cmd = &buffer[xpath_len];
                        if (strlen(cmd) >= 2) {
                            cmdtab_add(*init_table, buffer, (void*)xpath, (void*)EXTCMD_path);
                        }
                    }
                } while (test != EOF);
                
                pclose(stream);
            }
        }
    }

    /// Last, add native otter commands to the cmdtab.
    for (int i=0; i<(sizeof(otter_commands)/sizeof(cmd_t)); i++) {
        int rc;

        rc = cmdtab_add(*init_table, otter_commands[i].name, (void*)otter_commands[i].action, (void*)EXTCMD_null);
        if (rc != 0) {
            return -1;
        }

        /// Run command with dt=NULL to initialize the command
        ///@note This is specific to otter, it's not a requirement of cmdtab
        otter_commands[i].action(NULL, NULL, NULL, NULL, 0);
    }

    return 0;
}


int cmd_free(cmdtab_t* init_table) {
    
    if (init_table == NULL) {
        return -1;
    }
    /// First, free commands on xpath (should require nothing)
    
    /// Run command with dt=NULL to free the command
    ///@note This is specific to otter, it's not a requirement of cmdtab
    for (int i=0; i<(sizeof(otter_commands)/sizeof(cmd_t)); i++) {
        otter_commands[i].action(NULL, NULL, NULL, NULL, 0);
    }

    /// next, free cmdtab
    cmdtab_free(init_table);

    /// fourth, free the object itself
    free(init_table);

    return 0;
}



int cmd_run(const cmdtab_item_t* cmd, dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax) {
    int output;

    if (cmd == NULL) {
        return -1;
    }
    
    // handling of different command types
    switch ((otter_extcmd_t)cmd->extcmd) {
        case EXTCMD_null:
            //fprintf(stderr, "EXTCMD_null: inbytes=%d, src=%s\n", *inbytes, (char*)src);
            output = ((cmdaction_t)cmd->action)(dth, dst, inbytes, src, dstmax);
            break;

        case EXTCMD_path: {
            char xpath[512];
            char* cursor;
            FILE* stream;
            int rsize;
            //fprintf(stderr, "EXTCMD_path: inbytes=%d, src=%s\n", *inbytes, (char*)src);
            
            cursor          = xpath;
            cursor          = stpncpy(cursor, (const char*)(cmd->action), (&xpath[512] - cursor));
            cursor          = stpncpy(cursor, (const char*)(cmd->name), (&xpath[512] - cursor));
            cursor          = stpncpy(cursor, " ", (&xpath[512] - cursor));
            rsize           = (int)(&xpath[512] - cursor);
            rsize           = (*inbytes < rsize) ? *inbytes : (rsize-1);
            cursor[rsize]   = 0;
            memcpy(cursor, src, rsize);

            stream = popen(xpath, "r");
            if (stream == NULL) {
                output = -1;    ///@todo make sure this is correct error code
            }
            else {
                output = bintex_fs(stream, dst, (int)dstmax);
                pclose(stream);
            }
        } break;

        default:
            //fprintf(stderr, "No Command Extension found: inbytes=%d, src=%s\n", *inbytes, (char*)src);
            output = -2;
            break;
    }

    return output;
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




///@todo make utils_strmark()
size_t cmd_strmark(char* str, size_t max) {
    char* s1 = str;
    while ((*str!=0) && (*str!='\n') && (max!=0)) {
        max--;
        str++;
    }
    if (*str=='\n') *str = 0;
    
    return (str - s1);
}


///@todo Consider changing dterm_handle_t* input otter_app_t*
const cmdtab_item_t* cmd_quoteline_resolve(char* quoteline, dterm_handle_t* dth) {
    /// Get each line from the pipe.
    const cmdtab_item_t* cmdptr;
    char cmdname[32];
    int cmdlen;
    char* mark;
    char* cmdhead;
    otter_app_t* appdata;

    cmdlen  = (int)strlen(quoteline);
    cmdhead = quoteline;
    
    // Convert leading and trailing quotes into spaces
    mark = strchr(cmdhead, '"');
    if (mark == NULL) {
        return NULL;
    }
    *mark = ' ';
    mark = strrchr(cmdhead, '"');
    if (mark == NULL) {
        return NULL;
    }
    *mark = ' ';
    
    // Burn whitespace ahead of command, and burn trailing whitespace
    while (isspace(cmdhead[0])) {
        cmdhead++;
        --cmdlen;
    }
    while (isspace(cmdhead[cmdlen-1])) {
        cmdhead[cmdlen-1] = 0;
        --cmdlen;
    }
    
    // Then determine length until newline, or null.
    // then search/get command in list.
    appdata = dth->ext;
    cmdlen  = (int)cmd_strmark(cmdhead, (size_t)cmdlen);
    cmdlen  = cmd_getname(cmdname, cmdhead, sizeof(cmdname));
    cmdptr  = cmd_search(appdata->cmdtab, cmdname);
    
    // Rewrite loop->cmdline to remove the wrapper parts
    if (cmdptr != NULL) {
        mark    = &cmdhead[cmdlen];
        cmdhead = quoteline;
        while (*mark != 0) {
            *cmdhead++ = *mark++;
        }
        *cmdhead = 0;
    }

    return cmdptr;
}





const cmdtab_item_t* cmd_search(cmdtab_t* cmdtab, char *cmdname) {
    return cmdtab_search(cmdtab, cmdname);
}



const cmdtab_item_t* cmd_subsearch(cmdtab_t* cmdtab, char *namepart) {
    return cmdtab_subsearch(cmdtab, namepart);
}


