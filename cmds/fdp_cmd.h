/* Copyright 2020, JP Norair
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
/**
  * @file       fdp_generate.h
  * @author     JP Norair
  * @version    R100
  * @date       1 Dec 2017
  * @brief      Generate FDP API calls from otter-style text input
  * @ingroup    FDP
  *
  * File Data Protocol
  ******************************************************************************
  */

#ifndef _fdp_h_
#define _fdp_h_

#include <hbutils.h>
#include <stdio.h>
#include <stdint.h>


int fdp_init(void** handle);
void fdp_free(void* handle);

int fdp_generate(   void* handle, 
                    uint8_t* dst, size_t* dst_bytes, size_t dstmax, 
                    uint8_t* src, int* src_bytes
                );

int fdp_formatter(char* dst, size_t* dst_accum, size_t dst_limit,
                    HBFMT_Type fmt, uint8_t cmd, uint8_t** src, size_t srcsz);

#endif
