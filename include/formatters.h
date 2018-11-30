//
//  formatters.h
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef formatters_h
#define formatters_h

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cJSON.h"


typedef int (*mpipe_printer_t)(char*);


void fmt_printhex(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes, size_t cols);
void fmt_fprintalp(mpipe_printer_t puts_fn, cJSON* msgcall, uint8_t* src, size_t src_bytes);

void fmt_hexdump_raw(char* dst, uint8_t* src, size_t src_bytes);
char* fmt_hexdump_header(uint8_t* data);
char* fmt_crc(unsigned int crcqual);
char* fmt_time(time_t* tstamp);
void fmt_print_usage(const char* program_name);
int fmt_fprint_external(mpipe_printer_t puts_fn, const char* msgname, cJSON* msgcall, uint8_t* src, size_t size);








#endif /* formatters_h */
