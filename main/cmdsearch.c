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

// HB libraries
#include <cmdtab.h>
#include <bintex.h>
#include <judy.h>
#if OTTER_FEATURE(HBUILDER)
#   include <hbuilder.h>
#endif

// Standard libs
#include <string.h>
#include <stdio.h>
#include <ctype.h>




///@todo Commands to add
/// 1. Change addresses
/// 2. Have chuser command work on addresses (non-local), and change addresses as needed.
/// 3. ...


/// Binary Search Table for Commands

// sorted list of supported commands

///@todo this will need to be dynamic and build during startup based on an 
///      initialization file and plug-in libraries stored with the app.
///
typedef struct {
    const char      name[12]; 
    cmdaction_t     action; 
} cmd_t;


typedef struct {
    void* judy;
    size_t maxkey;
} envdict_handle_t;



static const cmd_t otter_commands[] = {
    { "bye",        &cmd_quit  },
    { "chuser",     &cmd_chuser },
    { "cmdls",      &cmd_cmdlist },
    { "null",       &app_null },
    { "quit",       &cmd_quit },
    { "raw",        &cmd_raw },
    { "set",        &cmd_set },
    { "sethome",    &cmd_sethome },
    { "su",         &cmd_su },
    { "useradd",    &cmd_useradd },
    { "whoami",     &cmd_whoami },
};

///@todo Command Table, hbuilder, and Judy handles (envdict) should get wrapped
///      into a cmd handle that is stored in dterm object.
static cmdtab_t cmdtab_default;
cmdtab_t* otter_cmdtab;

#if OTTER_FEATURE(HBUILDER)
static void* hbuilder_handle;
#endif

static envdict_handle_t envdict;


typedef enum {
    EXTCMD_null     = 0,
#   if OTTER_FEATURE(HBUILDER)
    EXTCMD_hbuilder,
#   endif
    EXTCMD_path,
    EXTCMD_MAX
} otter_extcmd_t;




int cmd_init(cmdtab_t* init_table, const char* xpath) {
    
    otter_cmdtab = (init_table == NULL) ? &cmdtab_default : init_table;

    envdict.judy    = NULL;
    envdict.maxkey  = 16;

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
                            cmdtab_add(otter_cmdtab, buffer, (void*)xpath, (void*)EXTCMD_path);
                        }
                    }
                } while (test != EOF);
                
                pclose(stream);
            }
        }
    }
    
    /// Second, if HBuilder is enabled, pass this table into the hbuilder 
    /// initializer.
#   if OTTER_FEATURE(HBUILDER)
        hbuilder_handle = hbuilder_init(otter_cmdtab, (void*)EXTCMD_hbuilder);
#   endif

    /// Last, Add Otter commands to the cmdtab.
    for (int i=0; i<(sizeof(otter_commands)/sizeof(cmd_t)); i++) {
        int rc;

        rc = cmdtab_add(otter_cmdtab, otter_commands[i].name, (void*)otter_commands[i].action, (void*)EXTCMD_null);
        if (rc != 0) {
            fprintf(stderr, "ERROR: cmdtab_add() from %s line %d returned %d.\n", __FUNCTION__, __LINE__, rc);
            return -1;
        }
        
        /// Run command with dt=NULL to initialize the command
        ///@note This is specific to otter, it's not a requirement of cmdtab
        otter_commands[i].action(NULL, NULL, NULL, NULL, 0);
    }
    
    /// Third: Initialize the command environment variables.
    /// These use libjudy to build the dictionary
    envdict.judy = judy_open((unsigned int)(4*envdict.maxkey), 0);
    if (envdict.judy == NULL) {
        return -2;
    }
    
    
    
    return 0;
}


int cmd_free(cmdtab_t* init_table) {
    
    init_table = (init_table == NULL) ? otter_cmdtab : init_table;
    
    /// First, free commands on xpath (should require nothing)
    
    /// Second, free hbuilder
#   if OTTER_FEATURE(HBUILDER)
        hbuilder_free(hbuilder_handle);
#   endif
    
    /// third, free cmdtab
    cmdtab_free(init_table);
    
    /// fourth, free envdict
    if (envdict.judy != NULL) {
        judy_close(envdict.judy);
    }
    
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
            
#       if OTTER_FEATURE(HBUILDER)
        case EXTCMD_hbuilder:
            //fprintf(stderr, "EXTCMD_hbuilder: inbytes=%d, src=%s\n", *inbytes, (char*)src);
            output = cmdext_hbuilder(hbuilder_handle, (void*)cmd->action, dth, dst, inbytes, src, dstmax);
            break;
#       endif

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




const cmdtab_item_t* cmd_search(char *cmdname) {
    return cmdtab_search(otter_cmdtab, cmdname);
}



const cmdtab_item_t* cmd_subsearch(char *namepart) {
    return cmdtab_subsearch(otter_cmdtab, namepart);
}





// ENVIRONMENT VARIABLE EXPERIMENTAL SECTION ================================


int cmd_envvar_new(dterm_handle_t* dth, const char* name, envdict_type_enum type, size_t size, void* data) {
    size_t total_size;
    uint32_t* jdata;
    unsigned char name_val[16];
    JudySlot* newcell;
    int rc = 0;
    
    switch (type) {
        case ENVDICT_string:
            size = strlen( (char*)data ) + 1;
            break;
        case ENVDICT_int:
            size *= sizeof(int);
            break;
        case ENVDICT_float:
            size *= sizeof(double);
            break;
        case ENVDICT_double:
            size *= sizeof(double);
            break;
       default:
            break;
    }
    
    total_size = (sizeof(uint32_t)*2) + size;
    if (total_size > (JUDY_seg - 8)) {
        rc = -1;
        goto cmd_envvar_new_END;
    }
    
    memset(name_val, 0, 16);
    strncpy((char*)name_val, name, 15);
    newcell = judy_cell(envdict.judy, name_val, (unsigned int)strlen((const char*)name_val));
    if (newcell == NULL) {
        rc = -2;
        goto cmd_envvar_new_END;
    }
    
    jdata = judy_data(envdict.judy, (unsigned int)total_size);
    if (jdata == NULL) {
        judy_del(envdict.judy);
        rc = -3;
        goto cmd_envvar_new_END;
    }
    
    *newcell = (JudySlot)jdata;
    jdata[0] = (uint32_t)type;
    jdata[1] = (uint32_t)size;
    if (size != 0) {
        memcpy(&jdata[2], data, size);
    }
    
    cmd_envvar_new_END:
    return rc;
}


int cmd_envvar_del(dterm_handle_t* dth, const char* name) {
    unsigned int name_len;
    void* val;
    int rc;
    
    name_len = (unsigned int)strlen(name);
    if (name_len > 15) {
        name_len = 15;
    }
    
    val = judy_slot(envdict.judy, (const unsigned char*)name, name_len);
    
    if (val != NULL) {
        judy_del(envdict.judy);
        rc = 0;
    }
    else {
        rc = -1;
    }
    
    return rc;
}


int cmd_envvar_get(dterm_handle_t* dth, size_t* varsize, void** vardata, const char* name) {
    unsigned int name_len;
    uint32_t* envdata;
    JudySlot* val;
    int rc;
    
    if ((varsize == NULL) || (vardata == NULL)) {
        return -1;
    }
    
    name_len = (unsigned int)strlen(name);
    if (name_len > 15) {
        name_len = 15;
    }
    
    val = judy_slot(envdict.judy, (const unsigned char*)name, name_len);
    
    if (val != NULL) {
        envdata     = (uint32_t*)*val;
        rc          = (envdict_type_enum)envdata[0];
        *varsize    = (size_t)envdata[1];
        *vardata    = &envdata[2];
    }
    else {
        rc = -2;
    }
    
    return rc;
}


int cmd_envvar_getint(dterm_handle_t* dth, const char* name) {
    size_t varsize;
    void* vardata;
    int rc;
    
    rc = cmd_envvar_get(dth, &varsize, &vardata, name);
    if (rc < 0) {
        rc = 0;
    }
    else {
        rc = ((int*)vardata)[0];
    }
    
    return rc;
}


// ENVIRONMENT VARIABLE EXPERIMENTAL SECTION ================================


