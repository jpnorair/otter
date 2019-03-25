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
  
///@todo cmdhistory.h/.c go into dterm library
///@todo consider replacing this with GNU History
  
// Local Headers
#include "cmdhistory.h"

// Standard C Libraries
#include <string.h>
#include <stdlib.h>





void ch_free(cmdhist* ch) {
    if (ch != NULL) {
        if (ch->history != NULL) {
            free(ch->history);
        }
        free(ch);
    }
}




cmdhist* ch_init(size_t hist_size) {
    cmdhist* ch;
    
    ///@todo make the size more dynamic than present
    if (hist_size == 0) {
        hist_size = CMD_HISTSIZE;
    }
    
    ch = malloc(sizeof(cmdhist));
    if (ch != NULL) {
        ch->size    = hist_size;
        ch->history = malloc(hist_size);
        if (ch->history != NULL) {
            ch->putcur      = ch->history;
            ch->getstart    = ch->history;
            ch->getend      = ch->history;
            ch->count       = 0;
            memset(ch->putcur, 0, ch->size);
        }
    }

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
			while (*cur != 0 && i++ < ch->size) cur = ch_inc(ch, cur);
            while (*cur == 0 && i++ < ch->size) cur = ch_inc(ch, cur);            
            cscur = cmdstr - 1;
		}
        
        cur = ch_inc(ch, cur);
	} while (*++cscur != 0 && ++i < ch->size);
	
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
    char* oldptr = cmdptr;

	do {
		*cmdptr = 0;
        cmdptr  = ch_inc(ch, cmdptr);
    } while ((cmdptr != oldptr) && (*cmdptr != 0));
    
    ch->count--;
}


/*
char* ch_next(cmdhist *ch) {
    if (ch->count == 0)
        return NULL;
    
    char *prev = NULL;
    char *cur = ch->getend;
    
    // shift to prev command
    while ((cur = ch_inc(ch, cur)) != ch->getstart && (*cur == 0));
    prev = cur;
    while ((cur = ch_inc(ch, cur)) != ch->getstart && (*cur != 0));
    
    // align cursors
    ch->getstart = prev;
    ch->getend = cur;
    
	return prev;
}
*/

char* ch_next(cmdhist *ch) {
    char* prev;
    char* cursor;

    if (ch->count == 0)
        return NULL;
    
    prev = ch->getstart;
    
    // ch->getend moves to the first 0 behind ch->getstart
    cursor = ch->getstart;
    while (*cursor != 0) {
        cursor = ch_dec(ch, cursor);
        if (cursor == ch->getstart) {
            break;
        }
    }
    ch->getend = cursor;
    
    // ch->getstart moves to the position ahead of the first 0 behind getend.
    while (*cursor == 0) {
        cursor = ch_dec(ch, cursor);
    }
    if (cursor != ch->getstart) {
        while (*cursor != 0) {
            cursor = ch_dec(ch, cursor);
        }
        cursor = ch_inc(ch, cursor);
        ch->getstart = cursor;
    }

    return prev;
}



char *ch_prev(cmdhist *ch) {
    char* cur;
    
    if (ch->count == 0)
        return 0;

    // shift to next command
    while ((ch->getstart = ch_dec(ch, ch->getstart)) != ch->getend && (*ch->getstart == 0));
    cur = ch->getstart;
    while ((ch->getstart = ch_dec(ch, ch->getstart)) != ch->getend && (*ch->getstart != 0));
    
    // align cursors
    if (ch->getstart != ch->getend)
        ch->getstart = ch_inc(ch, ch->getstart);
    ch->getend = cur;
    
	return ch->getstart;
}


char* ch_inc(cmdhist* ch, char* cmdcur) {
    cmdcur++;
    if (cmdcur >= (ch->history + ch->size)) {
        cmdcur = ch->history;
    }
    return cmdcur;
    
	//return ++cmdcur == (ch->history + ch->size) ? ch->history : cmdcur;
}


char *ch_dec(cmdhist *ch, char *cmdcur) {
    cmdcur--;
    if (cmdcur < ch->history) {
        cmdcur = ch->history+ch->size-1;
    }
    return cmdcur;

	//return cmdcur == (ch->history) ? ch->history+ch->size-1 : (cmdcur - 1);
}
