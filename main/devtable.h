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

#ifndef devtable_h
#define devtable_h


#include <stdint.h>

typedef void* devtab_node_t;



int devtab_init(void** new_handle);
void devtab_free(void* handle);

int devtab_insert(void* handle, uint64_t uid, uint16_t vid, void* intf_handle, void* rootkey, void* userkey);
devtab_node_t devtab_select(void* handle, uint64_t uid);
devtab_node_t devtab_select_vid(void* handle, uint16_t vid);

uint16_t devtab_get_vid(void* handle, devtab_node_t node);
void* devtab_get_intf(void* handle, devtab_node_t node);
void* devtab_get_rootctx(void* handle, devtab_node_t node);
void* devtab_get_userctx(void* handle, devtab_node_t node);

uint64_t devtab_lookup_uid(void* handle, uint16_t vid);
uint16_t devtab_lookup_vid(void* handle, uint64_t uid);
void* devtab_lookup_intf(void* handle, uint64_t uid);
void* devtab_lookup_rootctx(void* handle, uint64_t uid);
void* devtab_lookup_userctx(void* handle, uint64_t uid);


#endif
