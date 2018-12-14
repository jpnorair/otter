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

#ifndef cliopt_h
#define cliopt_h

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    INTF_mpipe  = 0,
    INTF_modbus = 1,
    INTF_max
} INTF_Type;

typedef enum {
    FORMAT_Default  = 0,
    FORMAT_Json     = 1,
    FORMAT_Bintex   = 2,
    FORMAT_Hex      = 3
} FORMAT_Type;


typedef struct {
    bool        verbose_on;
    bool        debug_on;
    bool        quiet_on;
    bool        dummy_tty;
    FORMAT_Type format;
    INTF_Type   intf;
    
    ///@todo determine if these global client vars belong here.
    int         dst_addr;
    int         src_addr;

} cliopt_t;


cliopt_t* cliopt_init(cliopt_t* new_master);


void cliopt_setverbose(bool val);
void cliopt_setdebug(bool val);
void cliopt_setdummy(bool val);
void cliopt_setquiet(bool val);

bool cliopt_isverbose(void);
bool cliopt_isdebug(void);
bool cliopt_isdummy(void);
bool cliopt_isquiet(void);

FORMAT_Type cliopt_getformat(void);

INTF_Type cliopt_getintf(void);

int cliopt_getdstaddr(void);
int cliopt_getsrcaddr(void);

void cliopt_setdstaddr(int addr);
void cliopt_setsrcaddr(int addr);



#endif /* cliopt_h */
