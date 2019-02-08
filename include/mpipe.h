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

#ifndef mpipe_h
#define mpipe_h

// Local Headers
#include "formatters.h"
#include "pktlist.h"
#include "subscribers.h"
#include "user.h"

// HB Library Headers
#include "cJSON.h"

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>


// MPipe Data Type(s)
///@todo bury these in a code module

typedef void* mpipe_handle_t;

typedef struct {
    int in;
    int out;
} mpipe_fd_t;



typedef struct {
    mpipe_handle_t  handle;
    
    pktlist_t*      tlist;           // should be used only by...
    pktlist_t*      rlist;           // should be used only by...
    user_endpoint_t* endpoint;
    subscr_handle_t subscribers;
    void*           dtprint;
    
    ///@todo this msgcall feature is deprecated
    cJSON*          msgcall;

    pthread_mutex_t*    dtwrite_mutex;
    pthread_mutex_t*    rlist_mutex;
    pthread_mutex_t*    tlist_mutex;
    pthread_mutex_t*    tlist_cond_mutex;
    pthread_cond_t*     tlist_cond;
//    pthread_mutex_t*    kill_mutex;
//    pthread_cond_t*     kill_cond;
    pthread_mutex_t*    pktrx_mutex;
    pthread_cond_t*     pktrx_cond;
} mpipe_arg_t;


int mpipe_init(mpipe_handle_t* handle, size_t num_intf);

void mpipe_deinit(mpipe_handle_t handle);

int mpipe_pollfd_alloc(mpipe_handle_t handle, struct pollfd** pollitems, short pollevents);

size_t mpipe_numintf_get(mpipe_handle_t handle);
mpipe_fd_t* mpipe_fds_get(mpipe_handle_t handle, int id);
const char* mpipe_file_get(mpipe_handle_t handle, int id);
void* mpipe_intf_get(mpipe_handle_t handle, int id);
void* mpipe_intf_fromfile(mpipe_handle_t handle, const char* file);

int mpipe_id_resolve(mpipe_handle_t handle, void* intfp);
mpipe_fd_t* mpipe_fds_resolve(void* intfp);
const char* mpipe_file_resolve(void* intfp);


int mpipe_close(mpipe_handle_t handle, int id);

int mpipe_opentty( mpipe_handle_t handle, int id,
                const char *dev, int baud, 
                int data_bits, char parity, int stop_bits, 
                int flowctrl, int dtr, int rts    );

int mpipe_reopen( mpipe_handle_t handle, int id);

void mpipe_flush(mpipe_handle_t handle, int id, size_t est_rembytes, int queue_selector);



// Thread Functions
///@todo move into mpipe_io section
void* mpipe_reader(void* args);
void* mpipe_writer(void* args);
void* mpipe_parser(void* args);







#endif
