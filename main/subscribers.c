//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "pktlist.h"

#include "cliopt.h"
#include "otter_cfg.h"

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

///@todo STRATEGY
/// - each open/close operation opens a subscription that has a condwait or some such semaphore
/// - Top level binary search for id
/// - id in search table is connected to a linked list of subscriptions for that ID
/// - at first, just store signal information from post, not the packet data.


typedef struct {
    int     id;
    
} subscr_userlut_t;

typedef struct usernode {
    subscr_userlut_t*   lut;
    size_t              subscriptions;
    struct usernode*    next;
    struct usernode*    prev;
} subscr_user_t;

typedef struct {
    subscr_user_t*      head;
} subscr_t;



int subscriber_init(void** handle, size_t payload_max, size_t frame_max) {

}

void subscriber_deinit(void* handle) {

}

subscr_node_t subscriber_new(void* handle) {
    /// return concealed pointer to subscr_user_t
    return NULL;
}

void subscriber_del(void* handle, subscr_node_t node) {

}


int subscriber_open(subscr_node_t node, int alp_id) {
    return 0;
}

int subscriber_close(subscr_node_t node, int alp_id) {
    return 0;
}


int subscriber_wait(subscr_node_t node, int alp_id) {
    return 0;
}


void subscriber_post(void* handle, int alp_id, uint8_t* payload, size_t size) {

}
