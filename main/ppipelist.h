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
/** 
  * 
  * @description ppipelist module maintains a list of ppipes.  In fact, it's
  *              just a different API for the ppipe array.  It requires ppipe.
  *              ppipelist is searchable.
  */

#ifndef ppipelist_h
#define ppipelist_h

#include <stdio.h>

#include "ppipe.h"


int ppipelist_init(const char* basepath);
void ppipelist_deinit(void);

int ppipelist_new(const char* prefix, const char* name, const char* fmode);

int ppipelist_del(const char* prefix, const char* name);


#ifdef USE_BIDIRECTIONAL_PIPES
int ppipelist_search(ppipe_fifo_t** dst, const char* prefix, const char* name, const char* mode);
#else
int ppipelist_search(ppipe_fifo_t** dst, const char* prefix, const char* name);
#endif

int ppipelist_putbinary(const char* prefix, const char* name, uint8_t* src, size_t size);

int ppipelist_puttext(const char* prefix, const char* name, char* src, size_t size);

int ppipelist_puthex(const char* prefix, const char* name, char* src, size_t size);

#endif /* ppipe_list_h */
