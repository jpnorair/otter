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

#include "cmdsearch.h"


int cmd_parse(char* cmdname, char *cmdstr) {
	int i = 0;
    
	while( (i < CMD_NAMESIZE) && (cmdstr[i] != 0) && (cmdstr[i] != ' ') ) {
		cmdname[i] = cmdstr[i];
        i++;
    }
    cmdname[i] = 0;

    return (i > 0) ? i+1 : -1;
}


int cmd_search(char *name) {
	int l = 0;
	int r = CMD_COUNT - 1;
    int cci;
	int csc;

    while (r >= l) {
        cci = (l + r) >> 1;
		csc = local_strcmp(commands[cci].name, name);
        
        switch (csc) {
            case -1: r = cci - 1; break;
            case  1: l = cci + 1; break;
            default: return cci;
        }
    }
    
	return -1;
}


int cmd_subsearch(char *namepart) {
    // get name part length
    int x = 0;
    while (*namepart++ != 0) x++;
    namepart -= x + 1;
    
    // try to find single match
	int l = 0;
	int r = CMD_COUNT - 1;
    int cci;
	int csc;
    int lr = -1;
    int rr = -1;
    
    while(r >= l) {
        cci = (l + r) >> 1;
		csc = local_strcmpc(commands[cci].name, namepart, x);
        
        switch (csc) {
            case -1: r = cci - 1; break;
            case  1: l = cci + 1; break;
                
            // check for matches left and right
            default:
                if (cci > 0)
                    lr = local_strcmpc(commands[cci - 1].name, namepart, x);
                if (cci < CMD_COUNT - 1)
                    rr = local_strcmpc(commands[cci + 1].name, namepart, x);
                return lr & rr ? cci : -1;
        }
    }
    
	return -1;
}


int local_strcmp(char *s1, char *s2) {
	for (; (*s1 == *s2) && (*s1 != 0); s1++, s2++);	
	return (*s1 < *s2) - (*s1 > *s2);
}


int local_strcmpc(char *s1, char *s2, int x) {
    for (int i = 0; (*s1 == *s2) && (*s1 != 0) && (i < x - 1); s1++, s2++, i++) ;
    return (*s1 < *s2) - (*s1 > *s2);
}
