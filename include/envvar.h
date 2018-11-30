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
// Otter Headers
#include "cliopt.h"
#include "cmds.h"
#include "dterm.h"
#include "otter_cfg.h"
//#include "test.h"

// HB Headers/Libraries
#include <judy.h>

// Standard C & POSIX Libraries
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>


typedef enum {
    ENVDICT_raw = 0,
    ENVDICT_string,
    ENVDICT_int,
    ENVDICT_float,
    ENVDICT_double,
    ENVDICT_MAX
} envdict_type_enum;



int envvar_init(void** handle, size_t max_namelen);
void envvar_free(void* handle);

int envvar_new(void* handle, const char* name, envdict_type_enum type, size_t size, void* data);


int envvar_del(void* handle, const char* name);


int envvar_get(void* handle, size_t* varsize, void** vardata, const char* name);


int envvar_getint(void* handle, const char* name);

