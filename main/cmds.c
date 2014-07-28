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

#include <stdio.h>
#include "cmds.h"


int searchCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
	fprintf(stderr, "echo: search %s\n", src);
	return 0;
}


int buildCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
	fprintf(stderr, "echo: build %s\n", src);
	return 0;
}


int saveCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
	fprintf(stderr, "echo: save %s\n", src);
	return 0;
}


int runCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
	fprintf(stderr, "echo: run %s\n", src);
	return 0;
}


int logCmd(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
	fprintf(stderr, "echo: log %s\n", src);
	return 0;
}
