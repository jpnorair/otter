//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef subscribers_h
#define subscribers_h

#include <stdio.h>
#include <stdint.h>


typedef void* subscr_t;

typedef void* subscr_handle_t;





int subscriber_init(subscr_handle_t* handle);


void subscriber_deinit(subscr_handle_t handle);


subscr_t subscriber_new(subscr_handle_t handle, int alp_id, size_t max_frames, size_t max_payload);


void subscriber_del(subscr_handle_t handle, subscr_t subscriber);


int subscriber_open(subscr_t subscriber, int sigmask);


int subscriber_close(subscr_t subscriber);


int subscriber_wait(subscr_t subscriber, int timeout_ms);


void subscriber_post(subscr_handle_t handle, int alp_id, int signal, uint8_t* payload, size_t size);

#endif
