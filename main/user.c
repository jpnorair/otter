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

#include "user.h"
#include "cliopt.h"
#include "otter_cfg.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if OTTER_FEATURE(SECURITY)
#   include <oteax.h>
#endif
#if OTTER_FEATURE(OTDB)
#   include <otdb.h>
#endif



typedef struct {
    USER_Type   usertype;
    uint64_t    id;
} user_t;

static user_t   current_user;


#if OTTER_FEATURE(SECURITY)
#   undef   OTTER_NUM_KEYS
#   define  OTTER_NUM_KEYS  2

    typedef struct {
        eax_ctx   ctx;
    } dllsctx_t;

    static uint32_t     dlls_nonce;
    static dllsctx_t    dlls_ctx[OTTER_NUM_KEYS];


    void sec_init(void);
    void sec_deinit(void);
    void sec_putnonce(uint8_t* dst, unsigned int total_size);
    uint32_t sec_getnonce(void);
    int sec_update_key(USER_Type usertype, uint8_t* keydata);
    int sec_encrypt(uint8_t* nonce, uint8_t* data, size_t datalen, unsigned int key_index);
    int sec_decrypt(uint8_t* nonce, uint8_t* data, size_t datalen, unsigned int key_index);
    
#endif







int user_init(void) {
    current_user.usertype   = USER_guest;
    current_user.id         = 0;
    
#   if OTTER_FEATURE(SECURITY)
    sec_init();
#   endif

    return 0;
}

void user_deinit(void) {
#   if OTTER_FEATURE(SECURITY)
    sec_deinit();
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


USER_Type user_typeval_get(void) {
    return current_user.usertype;
}


uint64_t user_idval_get(void) {
    return current_user.id;
}


int user_set_local(USER_Type usertype, KEY_Type keytype, uint8_t* keyval) {
#if (OTTER_FEATURE(SECURITY))
    unsigned int key_index;
    int rc;
    
    // Set to local ID, and with requested user type
    current_user.id         = 0;
    current_user.usertype   = usertype;
    
    // Use pre-existing key if keyval is null
    if (keyval != NULL) {
        rc = sec_update_key((unsigned int)usertype, (uint8_t*)keyval);
    }
    else {
        rc = 0;
    }
    
    return rc;

#endif
    return -1;
}


int user_set_db(USER_Type usertype, uint64_t uid) {
#if (OTTER_FEATURE(SECURITY) && OTTER_FEATURE(OTDB))

#else
    return -1;
#endif
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
            sec_putnonce(dst, 7);
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
        return 0;
    }
    if (key_index < (unsigned int)USER_guest) {
        int rc = sec_encrypt(front, front+7, payload_len, key_index);
        return (rc != 0) ? rc : (int)payload_len + 7 + 4;
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
    
    key_index = (unsigned int)usertype;
    
    if (key_index <= (unsigned int)USER_guest) {
        bytes_added = 3;    // 24 bit header
        frame_len  -= 3;    // 24 bit header
        
        if (key_index < (unsigned int)USER_guest) {
            if (0 != sec_decrypt(front, front+7, *frame_len-7-4, key_index)) {
                bytes_added = -1;       // error
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








#if OTTER_FEATURE(SECURITY)

/** High Level Cryptographic Interface Functions <BR>
  * ========================================================================<BR>
  * init, encrypt, decrypt
  */

void sec_init(void) {
    sec_deinit();
    dlls_nonce = arc4random();
}


void sec_deinit(void) {
/// clear all memory used for key storage, and free it if necessary.
    // Clear memory elements.  They are statically allocated in this case,
    // so no freeing is required.
    memset(dlls_ctx, 0, sizeof(dlls_ctx));
}


void sec_putnonce(uint8_t* dst, unsigned int total_size) {
/// Nonce is 4 bytes.
/// - If total_size < 4, only the LSBs of the nonce are written.  Not recommended.
/// - If total_size == 4, the whole nonce is written.
/// - If total_size > 4, the 4 byte nonce is placed at a latter position on dst,
///   thus the nonce gets padded with whatever bytes are already in dst.
    int         pad_bytes;
    int         write_bytes;
    uint32_t    output_nonce;
    
    write_bytes = 4;
    pad_bytes   = total_size - 4;
    if (pad_bytes > 0) {
        dst += pad_bytes;
    }
    else {
        write_bytes += pad_bytes;
    }
    
    /// Increment the internal nonce integer each time a nonce is put.
    /// It's also possible to change to network endian here, but it
    /// doesn't technically matter as long as the nonce data is 
    /// conveyed congruently.
    output_nonce = dlls_nonce++;
    
    memcpy(dst, &output_nonce, write_bytes);
}


uint32_t sec_getnonce(void) {
/// Increment the internal nonce integer each time a nonce is got.
    uint32_t output_nonce = dlls_nonce++;
    return output_nonce;
}


int sec_update_key(unsigned int key_index, uint8_t* keydata) {
    if (key_index < OTTER_NUM_KEYS) {
        eax_init_and_key((io_t*)keydata, &dlls_ctx[key_index].ctx);
        return 0;
    }
    return -1;
}



/// EAX cryptography is symmetric, so decrypt and encrypt are almost identical.

static int sub_do_crypto(uint8_t* nonce, uint8_t* data, size_t datalen, unsigned int key_index,
                        int (*EAXdrv_fn)(io_t*, unsigned long, eax_ctx*) ) {
    int retval;
    
    /// Error if key index is not available
    if (key_index >= OTTER_NUM_KEYS) {
        return -1;
    }
    
    retval = eax_init_message((const io_t*)nonce, (eax_ctx*)&dlls_ctx[key_index].ctx);
    if (retval == 0) {
        retval = EAXdrv_fn((io_t*)data, (unsigned long)datalen, (eax_ctx*)&dlls_ctx[key_index].ctx);
    }

    return retval;
}

int sec_encrypt(uint8_t* nonce, uint8_t* data, size_t datalen, unsigned int key_index) {
    return sub_do_crypto(nonce, data, datalen, key_index, &eax_encrypt);
}
int sec_decrypt(uint8_t* nonce, uint8_t* data, size_t datalen, unsigned int key_index) {
    return sub_do_crypto(nonce, data, datalen, key_index, &eax_decrypt);
}

#endif

