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

#ifndef cmdsearch_h
#define cmdsearch_h

// Local Dependencies
#include "cmds.h"
#include "dterm.h"

// External Dependencies
#include <cmdtab.h>




int cmd_init(cmdtab_t** init_table, const char* xpath);

int cmd_free(cmdtab_t* init_table);

int cmd_run(const cmdtab_item_t* cmd, dterm_handle_t* dth, uint8_t* dst, int* inbytes, uint8_t* src, size_t dstmax);



/** @brief Extracts command name from command line, returns command name length
  */
int cmd_getname(char* cmdname, char* cmdline, size_t max_cmdname);

size_t cmd_strmark(char* str, size_t max);
const cmdtab_item_t* cmd_quoteline_resolve(char* quoteline, dterm_handle_t* dth);



// searches for command by exact name
// returns command index or -1 if command not found
const cmdtab_item_t* cmd_search(cmdtab_t* cmdtab, char *name);


// searches for single command which name starts with namepart
// returns command index or -1 if command not found or there is more than one match
const cmdtab_item_t* cmd_subsearch(cmdtab_t* cmdtab, char *namepart);






#endif
