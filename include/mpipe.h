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
#include "otter_app.h"

// HB Library Headers
//#include <cJSON.h>

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>

// Queue Selectors for mpipe_flush()
#define MPIFLUSH    1
#define MPOFLUSH    2
#define MPIOFLUSH   3
#define MPODRAIN    4



// MPipe Data Type(s)
typedef void* mpipe_handle_t;

typedef struct {
    int in;
    int out;
} mpipe_fd_t;




typedef enum {
    MPINTF_null   = 0,
    MPINTF_tty,
    MPINTF_MAX
} mpipe_intf_enum;

typedef struct {
    char* path;
    int baud;
    int data_bits;
    int parity;
    int stop_bits;
    int flowctl;
    int dtr;
    int rts;
} mpipe_tty_t;

typedef struct {
    mpipe_intf_enum type;
    void*           params;
    mpipe_fd_t      fd;
} mpipe_intf_t;

typedef struct {
    mpipe_intf_t*   intf;
    size_t          size;
} mpipe_tab_t;






//typedef struct {
//    mpipe_handle_t  handle;
//
//    pktlist_t*      tlist;           // should be used only by...
//    pktlist_t*      rlist;           // should be used only by...
//    user_endpoint_t* endpoint;
//    subscr_handle_t subscribers;
//    void*           dtfd;
//
//    ///@todo this msgcall feature is deprecated
//    cJSON*          msgcall;
//
//    pthread_mutex_t*    iso_mutex;
//
//    pthread_mutex_t*    rlist_mutex;
//    pthread_mutex_t*    tlist_mutex;
//    pthread_mutex_t*    tlist_cond_mutex;
//    pthread_cond_t*     tlist_cond;
////    pthread_mutex_t*    kill_mutex;
////    pthread_cond_t*     kill_cond;
//    pthread_mutex_t*    pktrx_mutex;
//    pthread_cond_t*     pktrx_cond;
//} mpipe_arg_t;


int mpipe_init(mpipe_handle_t* handle, size_t num_intf);

void mpipe_deinit(mpipe_handle_t handle);

int mpipe_pollfd_alloc(mpipe_handle_t handle, struct pollfd** pollitems, short pollevents);

size_t mpipe_numintf_get(mpipe_handle_t handle);
mpipe_fd_t* mpipe_fds_get(mpipe_handle_t handle, int id);
const char* mpipe_file_get(mpipe_handle_t handle, int id);
void* mpipe_intf_get(mpipe_handle_t handle, int id);
void* mpipe_intf_fromfile(mpipe_handle_t handle, const char* file);

void mpipe_writeto_intf(void* intf, uint8_t* data, int data_bytes);


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


void mpipe_write_blocktx(void* intf);
void mpipe_write_unblocktx(void* intf);


// Thread Functions
///@todo move into mpipe_io section
void* mpipe_reader(void* args);
void* mpipe_writer(void* args);
void* mpipe_parser(void* args);







#endif
