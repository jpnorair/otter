//
//  pktlist.h
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef pktlist_h
#define pktlist_h

#include "user.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

typedef struct pkt {
    void*           parent;
    void*           intf;       //devtab_node_t   devnode;
    uint8_t*        buffer;
    size_t          size;
    int             crcqual;
    uint32_t        sequence;
    time_t          tstamp;
    struct pkt      *prev;
    struct pkt      *next;
} pkt_t;


///@todo put this inside C file
typedef struct {
    pkt_t*  front;
    pkt_t*  last;
    pkt_t*  cursor;
    size_t  size;
    size_t  max;
    int     txnonce;
    
    pthread_mutex_t* mutex;
} pktlist_t;



// Packet List Manipulation Functions
int pktlist_init(pktlist_t** plist, size_t max);
void pktlist_free(pktlist_t* plist);
void pktlist_empty(pktlist_t* plist);

pkt_t* pktlist_get(pktlist_t* plist);
pkt_t* pktlist_parse(int* errcode, pktlist_t* plist);
pkt_t* pktlist_add_tx(user_endpoint_t* endpoint, void* intf, pktlist_t* plist, uint8_t* data, size_t size);
pkt_t* pktlist_add_rx(user_endpoint_t* endpoint, void* intf, pktlist_t* plist,uint8_t* data, size_t size);

int pktlist_punt(pkt_t* pkt);
int pktlist_del(pkt_t* pkt);

int pktlist_del_sequence(pktlist_t* plist, uint32_t sequence);




#endif /* pktlist_h */
