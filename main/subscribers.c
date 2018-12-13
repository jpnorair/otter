//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "formatters.h"

#include "cliopt.h"
#include "otter_cfg.h"

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <time.h>




int subscriber_init(void** handle, size_t payload_max, size_t frame_max) {

}

void subscriber_deinit(void* handle) {

}

int subscriber_open(void* handle, int alp_id) {
    return 0;
}

int subscriber_close(void* handle, int alp_id) {
    return 0;
}


int subscriber_wait(void* handle, int alp_id) {
    return 0;
}


void subscriber_post(void* handle, int alp_id, uint8_t* payload, size_t size) {

}
