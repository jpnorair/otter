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

///@todo THIS MODULE SHOULD BE REMOVED


#include "user.h"

#include "crypto.h"
#include "cliopt.h"
#include "otter_cfg.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    USER_Type   usertype;
    uint64_t    id;
} user_t;

static user_t   current_user;




int user_init(void) {
    current_user.usertype   = USER_guest;
    current_user.id         = 0;
    
#   if OTTER_FEATURE(SECURITY)
    crypto_init();
#   endif

    return 0;
}

void user_deinit(void) {
#   if OTTER_FEATURE(SECURITY)
    crypto_deinit();
#   endif
}


const char* user_typestring_get(void) {
    static const char str_root[] = "root";
    static const char str_admin[] = "admin";
    static const char str_guest[] = "guest";

    switch (current_user.usertype) {
    case USER_root: return str_root;
    case USER_user: return str_admin;
    default:        return str_guest;
    }
}


USER_Type user_typeval_get(dterm_handle_t* dth) {
    return dth->endpoint.usertype;
}


uint64_t user_idval_get(void) {
    return current_user.id;
}


int user_set() {
    
    return 0;
}



int user_set_local(USER_Type usertype, KEY_Type keytype, uint8_t* keyval) {
/*
#if (OTTER_FEATURE(SECURITY))
    unsigned int key_index;
    int rc;
    
    // Set to local ID, and with requested user type
    current_user.id         = 0;
    current_user.usertype   = usertype;
    
    // Use pre-existing key if keyval is null
    if (keyval != NULL) {
        rc = crypto_update_key(0, (uint8_t*)keyval);
    }
    else {
        rc = 0;
    }
    
    return rc;

#endif
*/
    return -1;
}


int user_set_db(USER_Type usertype, uint64_t uid) {
    return -1;
}


int user_preencrypt(USER_Type usertype, uint64_t uid, uint8_t* dst, uint8_t* hdr24) {
#if (OTTER_FEATURE(SECURITY) && !OTTER_FEATURE(OTDB))
    unsigned int key_index;
    int bytes_added;
    
    if (dst == NULL) 
        return -3;

    key_index = (unsigned int)usertype;
    
    if (key_index <= (unsigned int)USER_guest) {
        if (hdr24 == NULL) {
            arc4random_buf(dst, 3);
        }
        bytes_added = 3;
        
        if (key_index < (unsigned int)USER_guest) {
            crypto_putnonce(dst, 7);
            bytes_added += 4;
        }
    }
    else {
        bytes_added = -1;
    }
    
    return bytes_added;

#elif (OTTER_FEATURE(SECURITY) && OTTER_FEATURE(OTDB))
    

#else
    return -1;
#endif
}


int user_encrypt(USER_Type usertype, uint64_t uid, uint8_t* front, size_t payload_len) {
#if (OTTER_FEATURE(SECURITY) && !OTTER_FEATURE(OTDB))
    unsigned int key_index;
    
    if (front == NULL)
        return -3;
    
    key_index = (unsigned int)usertype;
    if (key_index == (unsigned int)USER_guest) {
        return 3;
    }
    if (key_index < (unsigned int)USER_guest) {
        int rc = crypto_encrypt(front, front+7, payload_len, /*key_index*/ NULL);
        return (rc != 0) ? rc : 7 + 4;
    }

#elif (OTTER_FEATURE(SECURITY) && OTTER_FEATURE(OTDB))
    

#endif
    return -1;
}


int user_decrypt(USER_Type usertype, uint64_t uid, uint8_t* front, size_t* frame_len) {
#if (OTTER_FEATURE(SECURITY) && !OTTER_FEATURE(OTDB))
    unsigned int key_index;
    int bytes_added;
    
    if (front == NULL)      return -3;
    if (frame_len == NULL)  return -4;
    if (*frame_len < 11)    return -4;
    
    //fprintf(stderr, "framelen = %zu\n", *frame_len);
    
    key_index = (unsigned int)usertype;
    
    //fprintf(stderr, "keyindex = %d\n", key_index);
    
    if (key_index <= (unsigned int)USER_guest) {
        bytes_added = 3;    // 24 bit header
        *frame_len -= 3;    // 24 bit header
        
        if (key_index < (unsigned int)USER_guest) {
            //fprintf(stderr, "crypto_decrypt(%016llX, %016llX, %zu, %u)\n", (uint64_t)front, (uint64_t)(front+7), *frame_len-(4+4), key_index);
            if (0 != crypto_decrypt(front, front+7, *frame_len-(4+4), /*key_index*/ NULL)) {
                bytes_added = -1;       // error
                //fprintf(stderr, "crypto_decrypt() error\n");
            }
            else {
                bytes_added += 4;       // nonce (4)
                *frame_len  -= (4 + 4); // nonce (4) and MAC tag (4)
            }
        }
    }
    else {
        bytes_added = -1;   // error
    }
    
    return bytes_added;

#elif (OTTER_FEATURE(SECURITY) && OTTER_FEATURE(OTDB))
    
#else
    return -1;
#endif
}


