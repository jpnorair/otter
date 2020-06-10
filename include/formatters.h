//
//  formatters.h
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef formatters_h
#define formatters_h

#include "cliopt.h"

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include <cJSON.h>


typedef int (*mpipe_printer_t)(char*);



/// Generic formating functions (implemented in formatters.c)

int fmt_printhex(uint8_t* dst, size_t* dst_bytes, uint8_t** src, size_t src_bytes, size_t cols);
int fmt_fprintalp(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t src_bytes);

//void fmt_hexdump_raw(char* dst, uint8_t* src, size_t src_bytes);
int fmt_hexdump_raw(uint8_t* dst, size_t* dst_accum, uint8_t** src, size_t src_bytes);

const char* fmt_hexdump_header(uint8_t* data);
const char* fmt_crc(int crcqual, char* buf);
const char* fmt_time(time_t* tstamp, char* buf);

void fmt_print_usage(const char* program_name);
int fmt_fprint_external(mpipe_printer_t puts_fn, const char* msgname, cJSON* msgcall, uint8_t* src, size_t size);


/// Special Protocol formatters (implemented together with command function)
int fmt_fdp(char* dst, size_t* dst_accum, size_t dst_limit, FORMAT_Type fmt, uint8_t cmd, uint8_t** src, size_t srcsz);







#endif /* formatters_h */
