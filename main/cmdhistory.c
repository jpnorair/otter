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
#include "cmdhistory.h"

// Standard C Libraries
#include <string.h>






void ch_free(cmdhist* ch) {
    // Right now, ch is static memory
}




cmdhist* ch_init(cmdhist *ch) {
	ch->putcur      = ch->history;
	ch->getstart    = ch->history;
    ch->getend      = ch->history;
    ch->count       = 0;
    memset(ch->putcur, 0, CMD_HISTSIZE);
    
    return ch;
}


int ch_contains(cmdhist *ch, char *cmdstr) {
    if (ch->count == 0)
        return 0;
    
    int i = 0;
    char *cur = ch->getstart;
	char *cscur = cmdstr;			

	do {
        // if different - advance to the next cmd
		if (*cur != *cscur) {
			while (*cur != 0 && i++ < CMD_HISTSIZE) cur = ch_inc(ch, cur);
            while (*cur == 0 && i++ < CMD_HISTSIZE) cur = ch_inc(ch, cur);            
            cscur = cmdstr - 1;
		}
        
        cur = ch_inc(ch, cur);
	} while (*++cscur != 0 && ++i < CMD_HISTSIZE);
	
	return *cscur == 0 && (*cur == 0 || cur == ch->getstart);
}


void ch_add(cmdhist *ch, char *cmdstr) {
    ch->count++;
    ch->getstart = ch->putcur;

	do {
		if (*ch->putcur != 0) {
			ch_remove(ch, ch->putcur);
        }
		*ch->putcur = *cmdstr;
        ch->putcur  = ch_inc(ch, ch->putcur);
        
	} while ((ch->putcur != ch->getstart) && (*cmdstr++ != 0));
    
    ch->getend = ch->putcur;
}


void ch_remove(cmdhist *ch, char *cmdptr) {
    // no need to update getstart/getend cause its called from ch_add
    ch->count--;
	char *oldptr = cmdptr;

	do {
		*cmdptr = 0;
        cmdptr  = ch_inc(ch, cmdptr);
    } while ((cmdptr != oldptr) && (*cmdptr != 0));
}



char *ch_next(cmdhist *ch) {
    if (ch->count == 0)
        return 0;
    
    char *next = 0;
    char *cur = ch->getend;
    
    // shift to next command
    while ((cur = ch_inc(ch, cur)) != ch->getstart && (*cur == 0));
    next = cur;
    while ((cur = ch_inc(ch, cur)) != ch->getstart && (*cur != 0));
    
    // align cursors
    ch->getstart = next;
    ch->getend = cur;
    
	return next;
}


char *ch_prev(cmdhist *ch) {
    char* cur;
    
    if (ch->count == 0)
        return 0;

    // shift to prev command
    while ((ch->getstart = ch_dec(ch, ch->getstart)) != ch->getend && (*ch->getstart == 0));
    cur = ch->getstart;
    while ((ch->getstart = ch_dec(ch, ch->getstart)) != ch->getend && (*ch->getstart != 0));
    
    // align cursors
    if (ch->getstart != ch->getend)
        ch->getstart = ch_inc(ch, ch->getstart);
    ch->getend = cur;
    
	return ch->getstart;
}


char *ch_inc(cmdhist *ch, char *cmdcur) {    
	return ++cmdcur == ch->history + CMD_HISTSIZE ? ch->history : cmdcur;
}


char *ch_dec(cmdhist *ch, char *cmdcur) {
	return cmdcur == (ch->history) ? ch->history+CMD_HISTSIZE-1 : (cmdcur - 1);
}
