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

#ifndef otter_app_h
#define otter_app_h

// Local Dependencies
#include "mpipe.h"
#include "otter_cfg.h"
#include "pktlist.h"
#include "subscribers.h"
#include "user.h"

// External Dependencies
#include <cmdtab.h>

#include <pthread.h>

typedef struct {
    cmdtab_t*           cmdtab;
    pktlist_t*          tlist;
    pktlist_t*          rlist;
    
    ///@todo see if this can be made into a pointer
    user_endpoint_t     endpoint;
    
    void*               mpipe;
    subscr_handle_t     subscribers;
    void*               smut_handle;
    void*               dterm_parent;
    
    pthread_cond_t*     tlist_cond;
    pthread_mutex_t*    tlist_cond_mutex;
    pthread_cond_t*     pktrx_cond;
    pthread_mutex_t*    pktrx_mutex;
    
} otter_app_t;


#endif
