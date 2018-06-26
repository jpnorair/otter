/* Copyright 2018, JP Norair
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

#ifndef user_h
#define user_h

#include <stdint.h>

#include "otter_cfg.h"



typedef enum {
    USER_root   = 0,
    USER_user   = 1,
    USER_guest  = 2,
    USER_max    = 3
} USER_Type;



const char* user_typestring_get(void);
USER_Type user_typeval_get(void);


int user_localkey_new(USER_Type usertype, uint8_t* aes128_key);

int user_dbkey_set(USER_Type usertype, uint8_t* uid);


#endif /* user_h */
