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


#include "cliopt.h"


static cliopt_t* master;

cliopt_t* cliopt_init(cliopt_t* new_master) {
    master = new_master;
    
    //master->verbose_on  = false;
    //master->debug_on    = false;
    master->dummy_tty   = false;
    master->src_addr    = 1;    //1 = master
    master->dst_addr    = 0;    //0 = broadcast
    
    return master;
}

bool cliopt_isverbose(void) {
    return master->verbose_on;
}

bool cliopt_isdebug(void) {
    return master->debug_on;
}

bool cliopt_isquiet(void) {
    return master->quiet_on;
}

bool cliopt_isdummy(void) {
    return master->dummy_tty;
}

void cliopt_setverbose(bool val) {
    master->verbose_on = val;
}

void cliopt_setdebug(bool val) {
    master->debug_on = val;
}

void cliopt_setquiet(bool val) {
    master->quiet_on = val;
}

void cliopt_setdummy(bool val) {
    master->dummy_tty = val;
}


FORMAT_Type cliopt_getformat(void) {
    return master->format;
}

IO_Type cliopt_getio(void) {
    return master->io;
}

INTF_Type cliopt_getintf(void) {
    return master->intf;
}

int cliopt_getdstaddr(void) {
    return master->dst_addr;
}
void cliopt_setdstaddr(int addr) {
    master->dst_addr = addr;
}

int cliopt_getsrcaddr(void) {
    return master->src_addr;
}
void cliopt_setsrcaddr(int addr) {
    master->src_addr = addr;
}

size_t cliopt_getpoolsize(void) {
    return master->mempool_size;
}
void cliopt_setpoolsize(size_t poolsize) {
    master->mempool_size = poolsize;
}

int cliopt_gettimeout(void) {
    return master->timeout_ms;
}
void cliopt_settimeout(int timeout_ms) {
    master->timeout_ms = timeout_ms;
}

