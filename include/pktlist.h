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

typedef struct pkt {
    void*           intf;       //devtab_node_t   devnode;
    uint8_t*        buffer;
    size_t          size;
    int             crcqual;
    uint32_t        sequence;
    time_t          tstamp;
    struct pkt      *prev;
    struct pkt      *next;
} pkt_t;


typedef struct {
    pkt_t*  front;
    pkt_t*  last;
    pkt_t*  cursor;
    pkt_t*  marker;
    size_t  size;
    size_t  max;
    int     txnonce;
} pktlist_t;



// Packet List Manipulation Functions
int pktlist_init(pktlist_t* plist, size_t max);
void pktlist_free(pktlist_t* plist);
void pktlist_empty(pktlist_t* plist);
int pktlist_del(pktlist_t* plist, pkt_t* pkt);
int pktlist_getnew(pktlist_t* plist);

int pktlist_del_sequence(pktlist_t* plist, uint32_t sequence);

int pktlist_add_tx(user_endpoint_t* endpoint, void* intf, pktlist_t* plist, uint8_t* data, size_t size);
int pktlist_add_rx(user_endpoint_t* endpoint, void* intf, pktlist_t* plist,uint8_t* data, size_t size);


#endif /* pktlist_h */
