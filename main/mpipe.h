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

// Standard C & POSIX Libraries
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

// MPipe Data Type(s)
///@todo bury these in a code module

typedef int (*mpipe_printer_t)(char*);


typedef struct pkt {
    uint8_t*    buffer;
    size_t      size;
    int         crcqual;
    int         sequence;
    time_t      tstamp;
    struct pkt  *prev;
    struct pkt  *next;
} pkt_t;


typedef struct {
    pkt_t*  front;
    pkt_t*  last;
    pkt_t*  cursor;
    pkt_t*  marker;
    size_t  size;
} pktlist_t;


typedef struct {
    int     tty_fd;
    int     baudrate;
} mpipe_ctl_t;


typedef struct {
    mpipe_ctl_t*    mpctl;
    pktlist_t*      tlist;           // should be used only by...
    pktlist_t*      rlist;           // should be used only by...
    mpipe_printer_t puts_fn;

    pthread_mutex_t*    dtwrite_mutex;
    pthread_mutex_t*    rlist_mutex;
    pthread_mutex_t*    tlist_mutex;
    pthread_mutex_t*    tlist_cond_mutex;
    pthread_cond_t*     tlist_cond;
    pthread_mutex_t*    kill_mutex;
    pthread_cond_t*     kill_cond;
    pthread_mutex_t*    pktrx_mutex;
    pthread_cond_t*     pktrx_cond;
} mpipe_arg_t;






// MPipe exposed Functions (move to mpipe.h & mpipe.c)
void mpipe_freelists(pktlist_t* rlist, pktlist_t* tlist);
int mpipe_close(mpipe_ctl_t* mpctl);
int mpipe_open( mpipe_ctl_t* mpctl, 
                const char *dev, int baud, 
                int data_bits, char parity, int stop_bits, 
                int flowctrl, int dtr, int rts    );


// Packet List Manipulation Functions
int pktlist_init(pktlist_t* plist);
int pktlist_add(pktlist_t* list, uint8_t* data, size_t size);
int pktlist_del(pkt_t* pkt);
int pktlist_getnew(pktlist_t* plist);


// Thread Functions
void* mpipe_reader(void* args);
void* mpipe_writer(void* args);
void* mpipe_parser(void* args);







#endif
