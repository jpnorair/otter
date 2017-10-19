//
//  test.c
//  otter
//
//  Created by JP Norair on 21/9/17.
//  Copyright © 2017 JP Norair. All rights reserved.
//

#include "test.h"

#include <stdio.h>
#include <stdint.h>

void test_dumpbytes(const uint8_t* data, size_t datalen, const char* label) {
    
    fprintf(stderr, "%s\n", label);
    fprintf(stderr, "data-length = %zu\n", datalen);
    
    for (int16_t i=0; i<datalen; ) {
        if ((i % 16) == 0) {
            fprintf(stderr, "%04d: ", i);
        }
        
        fprintf(stderr, "%02X ", data[i]);
        
        i++;
        if ((i % 16) == 0) {
            fprintf(stderr, "\n");
        }
    }
    fprintf(stderr, "\n\n");
}
