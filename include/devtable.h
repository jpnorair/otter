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

typedef void* devtab_handle_t;

typedef void* devtab_node_t;

typedef struct {
    uint16_t    flags;
    uint16_t    vid;
    uint64_t    uid;
    void*       intf;
    void*       rootctx;
    void*       userctx;
} devtab_endpoint_t;



int devtab_init(devtab_handle_t* new_handle);
void devtab_free(devtab_handle_t handle);

int devtab_insert(devtab_handle_t handle, uint64_t uid, uint16_t vid, void* intf_handle, void* rootkey, void* userkey);
devtab_node_t devtab_select(devtab_handle_t handle, uint64_t uid);
devtab_node_t devtab_select_vid(devtab_handle_t handle, uint16_t vid);



int devtab_edit(devtab_handle_t handle, uint64_t uid, uint16_t vid, void* intf_handle, void* rootkey, void* userkey);
int devtab_edit_item(devtab_handle_t handle, devtab_node_t node, uint64_t uid, uint16_t vid, void* intf_handle, void* rootkey, void* userkey);

int devtab_remove(devtab_handle_t handle, uint64_t uid);
int devtab_unlist(devtab_handle_t handle, uint16_t vid);

devtab_endpoint_t* devtab_resolve_endpoint(devtab_node_t node);
uint16_t devtab_get_vid(devtab_handle_t handle, devtab_node_t node);
void* devtab_get_intf(devtab_handle_t handle, devtab_node_t node);
void* devtab_get_rootctx(devtab_handle_t handle, devtab_node_t node);
void* devtab_get_userctx(devtab_handle_t handle, devtab_node_t node);

uint64_t devtab_lookup_uid(devtab_handle_t handle, uint16_t vid);
uint16_t devtab_lookup_vid(devtab_handle_t handle, uint64_t uid);
void* devtab_lookup_intf(devtab_handle_t handle, uint64_t uid);
void* devtab_lookup_rootctx(devtab_handle_t handle, uint64_t uid);
void* devtab_lookup_userctx(devtab_handle_t handle, uint64_t uid);


#endif
