//
//  pktlist.h
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef pktlist_h
#define pktlist_h

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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



// Packet List Manipulation Functions
int pktlist_init(pktlist_t* plist);
int pktlist_add(pktlist_t* list, bool write_header, uint8_t* data, size_t size);
int pktlist_del(pktlist_t* plist, pkt_t* pkt);
int pktlist_getnew(pktlist_t* plist);



#endif /* pktlist_h */
