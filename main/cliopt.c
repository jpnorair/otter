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
    
    ///@note settings for modbus addressing, and perhaps future versions of
    ///      MPipe that have encryption.
    master->src_addr    = 0;
    master->dst_addr    = 1;
    
    ///@note this is for default guest access
    master->user_id     = 2;
    
    return master;
}

bool cliopt_isverbose(void) {
    return master->verbose_on;
}

bool cliopt_isdebug(void) {
    return master->debug_on;
}

FORMAT_Type cliopt_getformat(void) {
    return master->format;
}

INTF_Type cliopt_getintf(void) {
    return master->intf;
}

int cliopt_getuser(void) {
    return master->user_id;
}
void cliopt_setuser(int user_id) {
    master->user_id = user_id;
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
