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

#include "dterm.h"
#include "devtable.h"

#include <stdio.h>
#include <stdint.h>

#include "otter_cfg.h"



typedef enum {
    USER_root   = 0,
    USER_user   = 1,
    USER_guest  = 2,
    USER_max    = 3
} USER_Type;


typedef enum {
    KEY_AES128 = 0
} KEY_Type;



typedef struct {
    devtab_node_t   node;
    USER_Type       usertype;
} user_endpoint_t;




/** @brief Initialize User Module
  * @retval int     0 on success
  *
  * Run during startup of otter.
  */ 
int user_init(void);



/** @brief DeInitialize User Module
  * @retval None
  *
  * Run during shutdown of otter.
  */ 
void user_deinit(void);



/** @brief Get a string (e.g. guest, admin, root), corresponding to the active user
  * @retval const char*     user string
  *
  * Used by commands that need the user name string.
  */ 
const char* user_typestring_get(void);



/** @brief Get a type value corresponding to the active user
  * @retval USER_Type       Type Value, e.g. USER_guest, USER_admin, etc.
  *
  * Used by commands that need the user type value
  */ 
USER_Type user_typeval_get(dterm_handle_t* dth);



/** @brief Get the ID Value corresponding to the active user
  * @retval USER_Type       64 bit UID.  0 is local user.
  *
  * Used by commands that need the user ID value
  */ 
uint64_t user_idval_get(void);



/** @brief Supply a new key for a local user
  * @param usertype     (USER_Type) Type value of local user for whom to supply new key
  * @param keytype      (KEY_Type) Type of key.  Always AES128, presently.
  * @param keyval       (uint8_t*) key data.  Length depends on keytype parameter.   
  * @retval int         returns 0 on success
  *
  * Used by commands that set user keys
  */ 
int user_set_local(USER_Type usertype, KEY_Type keytype, uint8_t* keyval);



/** @brief Supply a new key for a user known to an external key DB.
  * @param usertype     (USER_Type) Type value of external user, which stipulates permissions.
  * @param uid          (uint64_t) User ID value, which together with usertype represent a DB key.
  * @retval int         returns 0 on success
  *
  * Used by commands that set user keys.  Is only supported by implementations
  * that utilize an external key database.  The typical external DB used for 
  * this purpose is OTDB.
  */ 
int user_set_db(USER_Type usertype, uint64_t uid);



/** @brief Put header and nonce to packet buffer, prior to encryption stage.
  * @param usertype     (USER_Type) Type value of user.
  * @param uid          (uint64_t) User ID value.  Set to 0 for local user.
  * @param front        (uint8_t*) front of frame buffer
  * @param hdr24        (uint8_t*) 24 bit header.  If NULL, a random number is used.
  * @retval int         Number of bytes added to frame, from front.
  *
  * user_preencrypt() drops a 24bit header and a 32bit nonce to the IV section
  * of the encrypted frame.  The header can be any data supplied by the caller.
  * 
  * user_preencrypt() returns the number of bytes that have been added to dst.
  * Typically this is 3 (header only, for guest access), or 7 (full IV placed).
  * If it is a negative value, there has been an error.
  */ 
int user_preencrypt(USER_Type usertype, uint64_t uid, uint8_t* front, uint8_t* hdr24);



/** @brief Put encrypted data, in-place, into frame
  * @param usertype     (USER_Type) Type value of user.
  * @param uid          (uint64_t) User ID value.  Set to 0 for local user.
  * @param front        (uint8_t*) front of frame buffer, at header position
  * @param payload_len  (size_t) size in bytes of frame payload
  * @retval int         Total size of the frame, from front. Negative on error.
  *
  * user_encrypt() should be used after user_preencrypt().  Typical usage is to
  * run user_preencrypt() to load the header and nonce, then load the payload
  * into the frame after the header & nonce, then call user_encrypt() on the
  * frame.  The pointer "front" points to the front of the entire frame, at the
  * position of the header.
  * 
  * The return value is the total number of bytes of the frame.  This includes
  * the header bytes (always 7), the payload length (supplied as argument), and
  * the 4 byte authentication tag footer.
  */ 
int user_encrypt(USER_Type usertype, uint64_t uid, uint8_t* front, size_t payload_len);



/** @brief Decrypt data, in-place, from frame
  * @param usertype     (USER_Type) Type value of user.
  * @param uid          (uint64_t) User ID value.  Set to 0 for local user.
  * @param front        (uint8_t*) front of frame buffer, at header position
  * @param frame_len    (size_t*) Input: size of encrypted frame. Output: size of decrypted frame.
  * @retval int         Byte offset from front of frame to payload. Negative on error.
  *
  * user_decrypt() should be used on an encrypted frame.  It will decrypt the
  * contents in place.  The header and nonce will be left unadjusted.  The 
  * payload will be decrypted and authenticated.
  * 
  * The return value is the byte offset from the front of the frame to the 
  * start of the decrypted payload.  Typically this is 7 bytes.  The "frame_len"
  * pointer argument is used as input to describe the length in bytes of the 
  * total, encrypted frame.  user_decrypt() will set the value during operation
  * such that, as output, frame_len represents the number of bytes of the 
  * payload only, with header, nonce, padding, and footer removed.
  */ 
int user_decrypt(USER_Type usertype, uint64_t uid, uint8_t* front, size_t* frame_len);


#endif /* user_h */
