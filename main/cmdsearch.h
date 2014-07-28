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

#ifndef otter_cmdsearch_h
#define otter_cmdsearch_h

#include "cmds.h"

// sorted list of supported commands
static const cmd commands[CMD_COUNT] = {
    { (char[CMD_NAMESIZE]) {'b','u','i','l','d', 0 , 0 , 0 }, buildCmd   },
    { (char[CMD_NAMESIZE]) {'b','u','i','l','d','2', 0 , 0 }, buildCmd   },
    { (char[CMD_NAMESIZE]) {'l','o','g', 0 , 0 , 0 , 0 , 0 }, logCmd     },
    { (char[CMD_NAMESIZE]) {'r','u','n', 0 , 0 , 0 , 0 , 0 }, runCmd     },
    { (char[CMD_NAMESIZE]) {'s','a','v','e', 0 , 0 , 0 , 0 }, saveCmd    },
    { (char[CMD_NAMESIZE]) {'s','e','a','r','c','h', 0 , 0 }, searchCmd  }
};


// parses input string for command name (chars before first whitespace)
// writes command name into cmdname array (assumes size of CMD_NAMESIZE + 1 )
// returns command params index or -1 if parsing failed
int cmd_parse(char* cmdname, char* cmd_parse);


// searches for command by exact name
// returns command index or -1 if command not found
int cmd_search(char *name);


// searches for single command which name starts with namepart
// returns command index or -1 if command not found or there is more than one match
int cmd_subsearch(char *namepart);


// comapres two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
int local_strcmp(char *s1, char *s2);


// comapres first x characters of two strings by alphabet,
// returns 0 - if equal, -1 - first one bigger, 1 - 2nd one bigger.
int local_strcmpc(char *s1, char *s2, int x);

#endif
