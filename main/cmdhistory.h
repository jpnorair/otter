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
  
#ifndef cmdhistory_h
#define cmdhistory_h

#define CMD_HISTSIZE 1024

// command history sate
typedef struct {
    char history[CMD_HISTSIZE];
    char *putcur;
    char *getstart;
    char *getend;
    int count;
} cmdhist;



// initializes command history
cmdhist* ch_init(cmdhist* ch);

void ch_free(cmdhist* ch);



// searches for first occurence of the string in command history
// returns 1 if contains, 0 - if not
int ch_contains(cmdhist *ch, char *cmdstr);


// adds command to history, if it's full overrides the next commands and
void ch_add(cmdhist *ch, char *cmdstr);


// removes command from history starting at supplied pointer
void ch_remove(cmdhist *ch, char *cmdptr);


// gets next command from history
// returns pointer to command in history or 0 if empty
char *ch_next(cmdhist *ch);


// gets previous command from history 
// returns pointer to command in history or 0 if empty
char *ch_prev(cmdhist *ch);


// increments command cursor, checks for overflow and wraps cursor, if required
// internal
// returns next command pointer
char *ch_inc(cmdhist *ch, char *cmdcur);


// decrements command cursor, checks for overflow and wraps cursor, if required
// internal
// returns previous command pointer
char *ch_dec(cmdhist *ch, char *cmdcur);

#endif
