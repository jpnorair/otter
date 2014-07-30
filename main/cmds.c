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
#include "cmds.h"

// Standard C & POSIX Libraries
#include <signal.h>
#include <stdio.h>




int cmd_quit(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    raise(SIGQUIT);
    return 0;
}








///@todo these commands are simply for test purposes right now. 



// Raw Protocol Entry
int app_raw(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "raw invoked %s\n", src);
    return 0;
}


// ID = 0
int app_null(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "null invoked %s\n", src);
    return 0;
}


// ID = 1
int app_file(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "file invoked %s\n", src);
    return 0;
}


// ID = 2
int app_sensor(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "sensor invoked %s\n", src);
    return 0;
}


// ID = 3
int app_sec(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "sec invoked %s\n", src);
    return 0;
}


// ID = 4
int app_log(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "logger invoked %s\n", src);
    return 0;
}


// ID = 5
int app_dforth(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "dforth invoked %s\n", src);
    return 0;
}


// ID = 6
int app_confit(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "confit invoked %s\n", src);
    return 0;
}


// ID = 7
int app_asapi(uint8_t* dst, uint8_t* src, size_t dstmax, size_t srcmax) {
    fprintf(stderr, "asapi invoked %s\n", src);
    return 0;
}


