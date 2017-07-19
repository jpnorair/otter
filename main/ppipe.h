/* Copyright 2017, JP Norair
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

#ifndef ppipe_h
#define ppipe_h

#include <stdio.h>


typedef struct {
    char*   fpath;
    FILE*   file;
    int     fd;
} ppipe_fifo_t;

typedef struct {
    char            basepath[256];
    ppipe_fifo_t*   fifo;
    size_t          num;
} ppipe_t;



int ppipe_init(const char* basepath);

void ppipe_deinit(void);

int ppipe_new(const char* prefix, const char* name, const char* fmode);

int ppipe_del(int ppd);

FILE* ppipe_getfile(int ppd);

const char* ppipe_getpath(int ppd);

ppipe_t* ppipe_ref(void);



#endif /* ppipe_h */
