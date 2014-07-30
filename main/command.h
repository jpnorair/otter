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

#ifndef dash_cmds_h
#define dash_cmds_h

#include <stdint.h>
#include <stdio.h>

#define CMD_NAMESIZE 8
#define CMD_COUNT 6

// arg1: dst buffer
// arg2: src buffer
// arg3: dst buffer max size
// arg4: src buffer max size
typedef int (*cmdMethod)(uint8_t*, uint8_t*, size_t, size_t);

typedef struct {
	char *name; 
	cmdMethod method; 
} cmd;


int searchCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax);
int buildCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax);
int saveCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax);
int runCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax);
int logCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax);

#endif
