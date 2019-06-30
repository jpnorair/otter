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


#include "crypto.h"
#include "otter_cfg.h"

#if OTTER_FEATURE(SECURITY)

#include <oteax.h>

#include <stdint.h>
#include <stdlib.h>

#ifdef __SHITTY_RANDOM__
#   include <time.h>
#   define RANDOM_U32(PTRU32) do { srand(time(NULL)); *PTRU32=(uint32_t)rand(); } while(0)
#else
#   ifdef __linux__
#       include <bsd/stdlib.h>
#   endif
#   define RANDOM_U32(PTRU32) do { *PTRU32 = arc4random(); } while(0)
#endif


#include <string.h>


static uint32_t dlls_nonce;



/** High Level Cryptographic Interface Functions <BR>
  * ========================================================================<BR>
  * init, encrypt, decrypt
  */

void crypto_init(void) {
    crypto_deinit();
    RANDOM_U32(&dlls_nonce);
    //dlls_nonce = arc4random();
}


void crypto_deinit(void) {
}


void crypto_putnonce(uint8_t* dst, unsigned int total_size) {
/// Nonce is 4 bytes.
/// - If total_size < 4, only the LSBs of the nonce are written.  Not recommended.
/// - If total_size == 4, the whole nonce is written.
/// - If total_size > 4, the 4 byte nonce is placed at a latter position on dst,
///   thus the nonce gets padded with whatever bytes are already in dst.
    int         pad_bytes;
    int         write_bytes;
    
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
    dlls_nonce++;
    dlls_nonce += (dlls_nonce == 0);
    
    memcpy(dst, &dlls_nonce, write_bytes);
}


uint32_t crypto_getnonce(void) {
    uint32_t output_nonce = dlls_nonce;
    return output_nonce;
}


int crypto_encrypt(uint8_t* nonce, uint8_t* data, size_t datalen, void* ctx) {
    return eax_encrypt_message(nonce, data, datalen, (eax_ctx*)ctx);
}


int crypto_decrypt(uint8_t* nonce, uint8_t* data, size_t datalen, void* ctx) {
    return eax_decrypt_message(nonce, data, datalen, (eax_ctx*)ctx);
}


#else

void crypto_init(void) {
}

void crypto_deinit(void) {
}

void crypto_putnonce(uint8_t* dst, unsigned int total_size) {
}

uint32_t crypto_getnonce(void) {
    return 0;
}

int crypto_encrypt(uint8_t* nonce, uint8_t* data, size_t datalen, void* ctx) {
    return -1;
}

int crypto_decrypt(uint8_t* nonce, uint8_t* data, size_t datalen, void* ctx) {
    return -1;
}

#endif
