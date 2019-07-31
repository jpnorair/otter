//
//  pktlist.c
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "devtable.h"
#include "pktlist.h"
#include "cliopt.h"
#include "debug.h"
#include "user.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <talloc.h>

#include "crc_calc_block.h"


///@todo Make pktlist a circular array (possibly via talloc arrays) in order
///      to reduce amount of allocations at runtime.



/// MPipe Reader & Writer Thread Functions
/// mpipe_add():    Adds a packet to the RX List (rlist) or TX List (tlist)
/// mpipe_del():    Deletes a packet from some place in the rlist or tlist
static void sub_pktlist_clear(pktlist_t* plist) {
    plist->front    = NULL;
    plist->last     = NULL;
    plist->cursor   = NULL;
    plist->size     = 0;
}

static void sub_pktlist_empty(pktlist_t* plist) {
    pkt_t* pkt = plist->front;

    while (pkt != NULL) {
        pkt_t* next_pkt = pkt->next;
        talloc_free(pkt);
        pkt = next_pkt;
    }
}



static void sub_unlinkpkt(pktlist_t* plist, pkt_t* pkt) {
    /// If packet was front of list, move front to next,
    if (plist->front == pkt) {
        plist->front = pkt->next;
    }
    
    /// If packet was last of list, move last to prev
    if (plist->last == pkt) {
        plist->last = pkt->prev;
    }
    
    /// Likewise, if the cursor was on the packet, advance it
    if (plist->cursor == pkt) {
        plist->cursor = pkt->next;
    }
    
    /// Stitch the list back together
    if (pkt->next != NULL) {
        pkt->next->prev = pkt->prev;
    }
    if (pkt->prev != NULL) {
        pkt->prev->next = pkt->next;
    }
}



static void sub_delpkt(pktlist_t* plist, pkt_t* pkt) {
    if (plist->size > 0) {
        sub_unlinkpkt(plist, pkt);
        talloc_free(pkt);
        plist->size--;
    }
    if (plist->size <= 0) {
        sub_pktlist_clear(plist);
    }
}



static void sub_frame_null(user_endpoint_t* endpoint, pkt_t* newpkt, uint8_t* data, size_t datalen) {
    memcpy(&newpkt->buffer[0], data, datalen);
}


static void sub_readframe_modbus(user_endpoint_t* endpoint, pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Modbus read process will remove the encrypted data
    int         mbcmd       = (data[1] & 255);
    size_t      frame_size  = datalen-2;            // Strip 2 byte CRC
    
    // Qualify CRC.
    newpkt->crcqual = mbcrc_calc_block(data, datalen);
    if (newpkt->crcqual != 0) {
        goto sub_readframe_modbus_LOAD;
    }
    
    // If the frame uses encryption, decrypt it.
    // Else, pass it through with CRC removed.
    if ((mbcmd >= 68) && (mbcmd <= 70)) {
        uint16_t    src_addr;
        uint8_t     hdr24[3];
        uint32_t    sequence;
        uint8_t*    seq8;
        int         offset;
        
        // Copy the source address to packet.  This is in all 68-70 packets
        newpkt->buffer[2]   = data[2];
        src_addr            = (uint16_t)data[2];
        
        // Get the sequence id (it's the 32bit end of the nonce)
        seq8    = (uint8_t*)&sequence;
        seq8[0] = data[3];
        seq8[1] = data[4];
        seq8[2] = data[5];
        seq8[3] = data[6];
        newpkt->sequence = sequence;
        
        // For 68/69 packets, there's an encrypted subframe.
        // frame_size will become the size of the decrypted data, at returned offset
        // If encryption failed, do not copy packet and leave size == 0
        mbcmd  -= 68;
        offset  = user_decrypt(endpoint, src_addr, 0, data, &frame_size);
        if (offset < 0) {
            newpkt->crcqual = -1;
            goto sub_readframe_modbus_LOAD;
        }
        
        // Realign headers
        hdr24[0]    = data[0];
        hdr24[1]    = data[1];
        hdr24[2]    = data[2];
        datalen     = frame_size + 3;
        data        = &data[offset-3];
        data[0]     = hdr24[0];
        data[1]     = hdr24[1];
        data[2]     = hdr24[2];
    }
    else {
        datalen -= 2;
        data    += 2;
    }
    
    sub_readframe_modbus_LOAD:
    ///@todo add cliopt for reporting bad packets
    if (cliopt_isquiet() == false) {
        newpkt->size = datalen;
        memcpy(newpkt->buffer, data, datalen);
    }
    else {
        // If newpkt->size == 0, this is considered error
        newpkt->size = 0;
    }
}


static void sub_writeframe_modbus(user_endpoint_t* endpoint, pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Adds 1, 3, or 11 bytes to payload depending on conditions
    int hdr_size;
    int pad_size;
    uint8_t mbaddr;
    devtab_endpoint_t* devEP = endpoint->node;
    
    /// Destination address is derived from the endpoint VID
    if (devEP->vid == 0)        mbaddr = cliopt_getdstaddr();
    else if (devEP->vid < 2)    mbaddr = 2;
    else if (devEP->vid > 249)  mbaddr = 249;
    else                        mbaddr = (uint8_t)devEP->vid;
    
    /// Basic Modbus header includes only destination address and cmd code
    newpkt->buffer[0]   = mbaddr;
    hdr_size            = 1;
    pad_size            = 0;
    
    /// Enhanced Modbus uses commands 68, 69, 70.
    /// MPipe/ALP encapsulation uses these commands.
    /// We know it's enhanced modbus if first byte is 0xC0/D0, which is the ALP
    /// Specifier as well as an illegal Function ID in regular modbus.
    if ((data[0] == 0xC0) || (data[0] == 0xD0)) {
        int cmdvariant;
        
        switch (endpoint->usertype) {
            case USER_root: cmdvariant = 0; break;
            case USER_user: cmdvariant = 1; break;
            default:        cmdvariant = 2; break;
        }
        newpkt->buffer[1]   = 68 + cmdvariant;
        newpkt->buffer[2]   = cliopt_getsrcaddr() & 0xFF;
        hdr_size            = user_preencrypt(
                                endpoint->usertype,
                                &newpkt->sequence,
                                &newpkt->buffer[0],
                                &newpkt->buffer[0]);
        
        memcpy(&newpkt->buffer[hdr_size], data, datalen);
        
        // Perform encryption
        if (cmdvariant < 2) {
            // Apply zero padding for alignment requirements
#           if (OTTER_PARAM_ENCALIGN != 1)
            while (datalen & (OTTER_PARAM_ENCALIGN-1)) {
                data[datalen] = 0;
                datalen++;
                pad_size++;
            }
#           endif

            hdr_size = user_encrypt(
                        endpoint,
                        0, devEP->uid,
                        &newpkt->buffer[0], datalen);
            
            if (hdr_size < 0) {
                goto sub_writeframe_modbus_ERR;
            }
        }
    }
    else {
        memcpy(&newpkt->buffer[hdr_size], data, datalen);
    }
    
    // need to add frame length to size value
    newpkt->size = newpkt->size + hdr_size + pad_size;
    return;
    
    sub_writeframe_modbus_ERR:
    newpkt->size = 0;
}


static void sub_writeframe_mpipe(user_endpoint_t* endpoint, pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Adds 8 bytes to packet
    newpkt->buffer[0] = 0xff;
    newpkt->buffer[1] = 0x55;
    newpkt->buffer[2] = 0;
    newpkt->buffer[3] = 0;
    newpkt->buffer[4] = (datalen >> 8) & 0xff;
    newpkt->buffer[5] = datalen & 0xff;
    newpkt->buffer[6] = 255 & newpkt->sequence;
    newpkt->buffer[7] = 0;      ///@todo Set Control Field here based on Cli
    newpkt->size     += 8;      // Header is 8 bytes, need to add this to size value.
    
    memcpy(&newpkt->buffer[8], data, datalen);
}


static void sub_footer_null(pkt_t* newpkt) {
}


static void sub_writefooter_mpipe(pkt_t* newpkt) {
/// Adds no bytes to packet
    uint16_t crcval;
    //newpkt->buffer[6]   = 255 & newpkt->sequence;
    crcval              = crc_calc_block(&newpkt->buffer[4], newpkt->size - 4);
    newpkt->buffer[2]   = (crcval >> 8) & 0xff;
    newpkt->buffer[3]   = crcval & 0xff;
}


static void sub_writefooter_modbus(pkt_t* newpkt) {
/// Adds two bytes to packet
    size_t crcpos               = newpkt->size;
    uint16_t crcval             = mbcrc_calc_block(&newpkt->buffer[0], newpkt->size);
    newpkt->size               += 2;
    newpkt->buffer[crcpos]      = crcval & 0xff;
    newpkt->buffer[crcpos+1]    = (crcval >> 8) & 0xff;
}


static pkt_t* sub_pktlist_add(user_endpoint_t* endpoint, void* intf, pktlist_t* plist, uint8_t* data, size_t size, bool iswrite) {
    size_t padding;
    void (*put_frame)(user_endpoint_t*, pkt_t*, uint8_t*, size_t);
    void (*put_footer)(pkt_t*);
    pkt_t* newpkt = NULL;
    int errcode = 0;
    
    if ((endpoint == NULL) || (plist == NULL)) {
        errcode = -1;
        goto sub_pktlist_add_ERR;
    }
    
    newpkt = talloc_size(plist, sizeof(pkt_t));
    if (newpkt == NULL) {
        errcode = -2;
        goto sub_pktlist_add_ERR;
    }
    
    // Offset is dependent if we are writing a header (8 bytes) or not.
    ///@todo this code can be optimized quite a lot
    if (iswrite) {
        switch (cliopt_getio()) {
            case IO_mpipe:
                padding     = 8;
                put_frame   = &sub_writeframe_mpipe;
                put_footer  = &sub_writefooter_mpipe;
                break;
            
            case IO_modbus:
                padding     = 17;
                put_frame   = &sub_writeframe_modbus;
                put_footer  = &sub_writefooter_modbus;
                break;
            
            default:
                padding     = 0;
                put_frame   = &sub_frame_null;
                put_footer  = &sub_footer_null;
                break;
        }
    }
    else {
        switch (cliopt_getio()) {
            case IO_mpipe:
                put_frame   = &sub_frame_null; ///@todo &sub_readframe_mpipe;
                break;
            
            case IO_modbus:
                put_frame   = &sub_readframe_modbus;
                break;
            
            default:
                put_frame   = &sub_frame_null;
                break;
        }
        put_footer  = &sub_footer_null;
        padding     = 0;
    }
    
    pthread_mutex_lock(&plist->mutex);
    
    // Sequence is written first, using the incrementer.  Protocol functions
    // may or may overwrite sequence with their own values.
    newpkt->sequence = plist->txnonce++;
    
    // Setup list connections for the new packet
    // Also allocate the buffer of the new packet
    // The starting size is the payload size, and put_frame() will modify it.
    newpkt->parent  = plist;
    newpkt->prev    = plist->last;
    newpkt->next    = NULL;
    newpkt->size    = size;
    padding         = ((padding + size + OTTER_PARAM_ENCALIGN-1) / OTTER_PARAM_ENCALIGN) * OTTER_PARAM_ENCALIGN;
    newpkt->buffer  = talloc_size(newpkt, padding);
    if (newpkt->buffer == NULL) {
        errcode =  -3;
        goto sub_pktlist_add_TERM;
    }
    
    // put_frame() with either write the TX frame or process the RX frame.
    // If there is no encryption, this doesn't do much, if anything, for RX.
    // If there's an error, we scrub the packet and exit with error.
    put_frame(endpoint, newpkt, data, size);
    if (newpkt->size == 0) {
        errcode = -4;
        goto sub_pktlist_add_TERM;
    }
    
    // Packet Frame is created successfully.
    // Save timestamp: this may or may not get used, but it's saved anyway.
    // The default sequence (which is available to frame generation) is
    // from the rotating nonce of the plist.
    newpkt->tstamp = time(NULL);

    ///@note If no explicit interface, use the interface attached to dterm's
    /// (dterm is the controlling terminal) active endpoint.  "Active endpoint"
    /// stipulates a device on the network and its access level, typically
    /// specified by mknode and/or chuser commands.  In normal usage, packets
    /// for transmission are implicitly routed and packets that are received
    /// are explicitly routed.
    if (intf == NULL) {
        devtab_endpoint_t* dev_ep = devtab_resolve_endpoint(endpoint->node);
        intf = dev_ep->intf;
    }
    newpkt->intf = intf;
    
    // List is empty, so start the list
    if (plist->last == NULL) {
        plist->size         = 0;
        plist->front        = newpkt;
        plist->last         = newpkt;
        plist->cursor       = newpkt;
    }
    // List is not empty, so simply extend the list.
    // set the cursor to the new packet if it points to NULL (end)
    else {
        newpkt->prev->next  = newpkt;
        plist->last         = newpkt;
        
        if (plist->cursor == NULL) {
            plist->cursor   = newpkt;
        }
    }
    
    put_footer(newpkt);
    
    // Increment the list size to account for new packet.
    // If the list is longer than max allowable size, delete oldest packet
    plist->size++;
    if (plist->size > plist->max) {
        sub_delpkt(plist, plist->front);
    }
    
    sub_pktlist_add_TERM:
    if ((newpkt != NULL) && (errcode != 0)) {
        talloc_free(newpkt);
        newpkt = NULL;
    }
    
    pthread_mutex_unlock(&plist->mutex);
    return newpkt;
    
    sub_pktlist_add_ERR:
    return NULL;
}




int pktlist_init(pktlist_t** plist, size_t max) {
    pktlist_t* newlist = NULL;
    int rc = 0;

    if ((plist == NULL) || (max == 0)) {
        rc = -1;
        goto pktlist_init_ERR;
    }
    
    talloc_disable_null_tracking();
    newlist = talloc_size(NULL, sizeof(pktlist_t));
    if (newlist == NULL) {
        rc = -2;
        goto pktlist_init_ERR;
    }
    if (pthread_mutex_init(&newlist->mutex, NULL) != 0) {
        rc = -3;
        goto pktlist_init_ERR;
    }
    
    sub_pktlist_clear(newlist);
    newlist->txnonce  = 0;
    newlist->max      = max;
    *plist = newlist;
    return 0;
    
    pktlist_init_ERR:
    talloc_free(newlist);
    return rc;
}


void pktlist_free(pktlist_t* plist) {
    if (plist != NULL) {
        pthread_mutex_destroy(&plist->mutex);
        talloc_free(plist);
    }
}


void pktlist_empty(pktlist_t* plist) {
    if (plist != NULL) {
        pthread_mutex_lock(&plist->mutex);
        sub_pktlist_empty(plist);
        sub_pktlist_clear(plist);
        pthread_mutex_unlock(&plist->mutex);
    }
}



pkt_t* pktlist_add_tx(user_endpoint_t* endpoint, void* intf, pktlist_t* plist, uint8_t* data, size_t size) {
    ///@todo endpoint vs. intf NULL check
    return sub_pktlist_add(endpoint, intf, plist, data, size, true);
}

pkt_t* pktlist_add_rx(user_endpoint_t* endpoint, void* intf,  pktlist_t* plist, uint8_t* data, size_t size) {
    ///@todo endpoint vs. intf NULL check
    pkt_t* rc;
    
    rc = sub_pktlist_add(endpoint, intf, plist, data, size, false);
    
    HEX_DUMP(plist->last->buffer, plist->last->size, "%zu Bytes Queued\n", plist->last->size);

    return rc;
}



int pktlist_del(pkt_t* pkt) {
    pktlist_t* plist;

    if (pkt == NULL) {
        return -1;
    }
    if (pkt->parent == NULL) {
        return -2;
    }
    plist = pkt->parent;

    pthread_mutex_lock(&plist->mutex);
    sub_delpkt(plist, pkt);
    pthread_mutex_unlock(&plist->mutex);
    return 0;
}



int pktlist_punt(pkt_t* pkt) {
    pktlist_t* plist;

    if (pkt == NULL) {
        return -1;
    }
    if (pkt->parent == NULL) {
        return -2;
    }
    
    plist = pkt->parent;
    sub_unlinkpkt(plist, pkt);
    
    // Move to last
    plist->last->next   = pkt;
    pkt->prev           = plist->last;
    pkt->next           = NULL;
    plist->last         = pkt;
    
    return 0;
}



int pktlist_del_sequence(pktlist_t* plist, uint32_t sequence) {
    pkt_t* pkt;
    int rc = 0;

    if (plist != NULL) {
        pthread_mutex_lock(&plist->mutex);
        pkt = plist->front;
    
        while (pkt != plist->cursor) {
            pkt_t* next_pkt = pkt->next;
            if (pkt->sequence == sequence) {
                sub_delpkt(plist, pkt);
                rc++;
            }
            pkt = next_pkt;
        }
        pthread_mutex_unlock(&plist->mutex);
    }
    
    return rc;
}




pkt_t* pktlist_get(pktlist_t* plist) {
    pkt_t* pkt = NULL;

    if (plist != NULL) {
        pthread_mutex_lock(&plist->mutex);
        if (plist->cursor != NULL) {
            pkt = plist->cursor;
            plist->cursor = plist->cursor->next;
        }
        pthread_mutex_unlock(&plist->mutex);
    }

    return pkt;
}


pkt_t* pktlist_parse(int* errcode, pktlist_t* plist) {
    IO_Type intf;
    int outcode;
    pkt_t* pkt = NULL;
    //time_t      seconds;

    // packet list is not allocated -- that's a serious error
    if (plist == NULL) {
        outcode = -11;
    }
    // packet list is fine
    else {
        pthread_mutex_lock(&plist->mutex);
        
        // pktlist is empty if cursor == NULL
        if (plist->cursor == NULL) {
            outcode = -1;
        }
        else {
            pkt             = plist->cursor;
            plist->cursor   = plist->cursor->next;
            pkt->tstamp     = time(NULL);   //;localtime(&seconds);
            intf            = cliopt_getio();
            
            // MPipe uses Sequence-ID for message matching
            ///@todo move this into pktlist_add() via sub_mpipe_readframe()
            if (intf == IO_mpipe) {
                uint16_t crc_val;
                uint16_t crc_comp;
            
                pkt->sequence   = pkt->buffer[4];
                crc_val         = (pkt->buffer[0] << 8) + pkt->buffer[1];
                crc_comp        = crc_calc_block(&pkt->buffer[2], pkt->size-2);
                pkt->crcqual    = (crc_comp ^ crc_val);
            }
            else if (intf == IO_modbus) {
                // do nothing
            }
            else {
                pkt->crcqual  = 0;
            }
            
            outcode = 0;
        }
        
        pthread_mutex_unlock(&plist->mutex);
    }
    
    if (errcode != NULL) {
        *errcode = outcode;
    }
    return pkt;
}
