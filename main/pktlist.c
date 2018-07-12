//
//  pktlist.c
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "pktlist.h"
#include "cliopt.h"
#include "user.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "crc_calc_block.h"



/// MPipe Reader & Writer Thread Functions
/// mpipe_add():    Adds a packet to the RX List (rlist) or TX List (tlist)
/// mpipe_del():    Deletes a packet from some place in the rlist or tlist

int pktlist_init(pktlist_t* plist) {
    plist->front    = NULL;
    plist->last     = NULL;
    plist->cursor   = NULL;
    plist->marker   = NULL;
    plist->size     = 0;
    plist->txnonce  = 0;
    
    return 0;
}



void sub_frame_null(pkt_t* newpkt, uint8_t* data, size_t datalen) {
    memcpy(&newpkt->buffer[0], data, datalen);
}


void sub_readframe_modbus(pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Modbus read process will remove the encrypted data
    int         mbcmd       = data[1];
    size_t      frame_size  = datalen-2;            // Strip 2 byte CRC
    
    // Qualify CRC.
    newpkt->crcqual = mbcrc_calc_block(data, datalen);
    if (newpkt->crcqual != 0) {
        if (cliopt_isverbose()) goto sub_readframe_modbus_LOAD;
        else                    goto sub_readframe_modbus_ERR;
    }
        
    // If the frame uses encryption, decrypt it.
    // Else, pass it through with CRC removed.
    if ((mbcmd >= 68) && (mbcmd <= 70)) {
        uint64_t    src_addr;
        uint8_t     hdr24[3];
        int         offset;
        
        // Copy the source address to packet.  This is in all 68-70 packets
        newpkt->buffer[2]   = data[2];
        src_addr            = (uint64_t)data[2];
        
        // Encryption I/O debugging
        for (int i=0; i<datalen; i++) {
            fprintf(stderr, "%02X ", data[i]);
        }
        fprintf(stderr, "\n");
        
        // For 68/69 packets, there's an encrypted subframe.
        // frame_size will become the size of the decrypted data, at returned offset
        // If encryption failed, do not copy packet and leave size == 0
        mbcmd  -= 68;
        offset  = user_decrypt((USER_Type)mbcmd, src_addr, data, &frame_size);
        
        for (int i=0; i<datalen; i++) {
            fprintf(stderr, "%02X ", data[i]);
        }
        fprintf(stderr, "\n");
        
        if (offset < 0) {
            newpkt->crcqual = -1;
            if (cliopt_isverbose()) goto sub_readframe_modbus_LOAD;
            else                    goto sub_readframe_modbus_ERR;
        }
        
        // Encryption I/O debugging
        for (int i=0; i<datalen; i++) {
            fprintf(stderr, "%02X ", data[i]);
        }
        fprintf(stderr, "\n");
        
        // Realign headers
        hdr24[0]    = data[0];
        hdr24[1]    = data[1];
        hdr24[2]    = data[2];
        datalen     = frame_size + 3;
        data        = &data[offset-3];
        data[0]     = hdr24[0];
        data[1]     = hdr24[1];
        data[2]     = hdr24[2];
        
        // Encryption I/O debugging
        for (int i=0; i<datalen; i++) {
            fprintf(stderr, "%02X ", newpkt->buffer[i]);
        }
        fprintf(stderr, "\n");
    }
    else {
        datalen -= 2;
        data    += 2;
    }
    
    sub_readframe_modbus_LOAD:
    newpkt->size = datalen;
    memcpy(newpkt->buffer, data, datalen);
    return;
    
    sub_readframe_modbus_ERR:
    // If newpkt->size == 0, this is considered error
    newpkt->size = 0;
}


void sub_writeframe_modbus(pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Adds 1, 3, or 11 bytes to payload depending on conditions
    int hdr_size;
    int pad_size;
    
    /// Basic Modbus just puts the destination address.
    newpkt->buffer[0]   = cliopt_getdstaddr() & 0xFF;
    hdr_size            = 1;
    pad_size            = 0;
    
    /// Enhanced Modbus uses commands 68, 69, 70.
    /// MPipe/ALP encapsulation uses these commands.
    /// We know it's enhanced modbus if first byte is 0xC0/D0, which is the ALP
    /// Specifier as well as an illegal Function ID in regular modbus.
    if ((data[0] == 0xC0) || (data[0] == 0xD0)) {
        int         cmdvariant;
        USER_Type   usertype;
        
        usertype = user_typeval_get();
        switch (usertype) {
            case USER_root: cmdvariant = 0; break;
            case USER_user: cmdvariant = 1; break;
            default:        cmdvariant = 2; break;
        }
        newpkt->buffer[1]   = 68 + cmdvariant;
        newpkt->buffer[2]   = cliopt_getsrcaddr() & 0xFF;
        hdr_size            = user_preencrypt(usertype, user_idval_get(), &newpkt->buffer[0], &newpkt->buffer[0]);
        memcpy(&newpkt->buffer[hdr_size], data, datalen);
        
        // Perform encryption
        if (cmdvariant < 2) {
            // Encryption I/O debugging
//            for (int i=0; i<(hdr_size+datalen); i++) {
//                fprintf(stderr, "%02X ", newpkt->buffer[i]);
//            }
//            fprintf(stderr, "\n");
        
            // Apply zero padding for alignment requirements
#           if (OTTER_PARAM_ENCALIGN != 1)
            while (datalen & (OTTER_PARAM_ENCALIGN-1)) {
                data[datalen] = 0;
                datalen++;
                pad_size++;
            }
#           endif
            
            hdr_size = user_encrypt(usertype, user_idval_get(), &newpkt->buffer[0], datalen);
            if (hdr_size < 0) {
                goto sub_writeframe_modbus_ERR;
            }

            // Encryption I/O debugging
//            for (int i=0; i<(hdr_size+datalen); i++) {
//                fprintf(stderr, "%02X ", newpkt->buffer[i]);
//            }
//            fprintf(stderr, "\n");
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


void sub_writeframe_mpipe(pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Adds 8 bytes to packet
    newpkt->buffer[0] = 0xff;
    newpkt->buffer[1] = 0x55;
    newpkt->buffer[2] = 0;
    newpkt->buffer[3] = 0;
    newpkt->buffer[4] = (datalen >> 8) & 0xff;
    newpkt->buffer[5] = datalen & 0xff;
    newpkt->buffer[6] = 0;
    newpkt->buffer[7] = 0;      ///@todo Set Control Field here based on Cli
    
    newpkt->size     += 8;      // Header is 8 bytes, need to add this to size value.
    
    memcpy(&newpkt->buffer[8], data, datalen);
}


void sub_footer_null(pkt_t* newpkt) {
}


void sub_writefooter_mpipe(pkt_t* newpkt) {
/// Adds no bytes to packet
    uint16_t crcval;
    newpkt->buffer[6]   = newpkt->sequence;
    crcval              = crc_calc_block(&newpkt->buffer[4], newpkt->size - 4);
    newpkt->buffer[2]   = (crcval >> 8) & 0xff;
    newpkt->buffer[3]   = crcval & 0xff;
}


void sub_writefooter_modbus(pkt_t* newpkt) {
/// Adds two bytes to packet
    size_t crcpos               = newpkt->size;
    uint16_t crcval             = mbcrc_calc_block(&newpkt->buffer[0], newpkt->size);
    newpkt->size               += 2;
    newpkt->buffer[crcpos]      = crcval & 0xff;
    newpkt->buffer[crcpos+1]    = (crcval >> 8) & 0xff;
}




int pktlist_add(pktlist_t* plist, bool write_header, uint8_t* data, size_t size) {
    pkt_t* newpkt;
    size_t max_overhead;
    void (*put_frame)(pkt_t*, uint8_t*, size_t);
    void (*put_footer)(pkt_t*);
    
    if (plist == NULL) {
        return -1;
    }
    
    newpkt = malloc(sizeof(pkt_t));
    if (newpkt == NULL) {
        return -2;
    }
    
    // Offset is dependent if we are writing a header (8 bytes) or not.
    ///@todo this code can be optimized quite a lot
    if (write_header) {
        switch (cliopt_getintf()) {
            case INTF_mpipe:    
                max_overhead= 8;
                put_frame   = &sub_writeframe_mpipe;
                put_footer  = &sub_writefooter_mpipe;
                break;
            
            case INTF_modbus:
                max_overhead= 12;
                put_frame   = &sub_writeframe_modbus;
                put_footer  = &sub_writefooter_modbus;
                break;
            
            default:
                max_overhead= 0;
                put_frame   = &sub_frame_null;
                put_footer  = &sub_footer_null;
                break;
        }
    }
    else {
        switch (cliopt_getintf()) {
            case INTF_mpipe:    
                put_frame   = &sub_frame_null; ///@todo &sub_readframe_mpipe;
                break;
            
            case INTF_modbus:
                put_frame   = &sub_readframe_modbus;
                break;
            
            default:
                put_frame   = &sub_frame_null;
                break;
        }
        put_footer  = &sub_footer_null;
        max_overhead= 0;
    }

    // Setup list connections for the new packet
    // Also allocate the buffer of the new packet
    // The starting size is the payload size, and put_frame() will modify it.
    newpkt->size    = size;
    newpkt->buffer  = malloc(size + max_overhead + OTTER_PARAM_ENCALIGN-1);
    if (newpkt->buffer == NULL) {
        free(newpkt);
        return -3;
    }
    
    // put_frame() with either write the TX frame or process the RX frame.
    // If there is no encryption, this doesn't do much, if anything, for RX.
    // If there's an error, we scrub the packet and exit with error.
    put_frame(newpkt, data, size);
    if (newpkt->size == 0) {
        free(newpkt->buffer);
        free(newpkt);
        return -4;
    }
    
    // Packet Frame is created successfully.
    // Save timestamp: this may or may not get used, but it's saved anyway.
    // The default sequence (which is available to frame generation) is
    // from the rotating nonce of the plist.
    newpkt->prev        = plist->last;
    newpkt->next        = NULL;
    newpkt->tstamp      = time(NULL);
    newpkt->sequence    = plist->txnonce++;
    
    // List is empty, so start the list
    if (plist->last == NULL) {
        newpkt->sequence    = 0;
        plist->size         = 0;
        plist->front        = newpkt;
        plist->last         = newpkt;
        plist->cursor       = newpkt;
        plist->marker       = newpkt;
    }
    // List is not empty, so simply extend the list.
    // set the cursor to the new packet if it points to NULL (end)
    else {
        //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        newpkt->sequence    = plist->last->sequence + 1;
        newpkt->prev->next  = newpkt;
        plist->last         = newpkt;
        
        if (plist->cursor == NULL) {
            plist->cursor   = newpkt;
        }
    }
    
    ///@todo Move Sequence Number entry and CRC entry to somewhere in writer
    ///      thread, so that it can be retransmitted with new sequence
    put_footer(newpkt);
    
    // Increment the list size to account for new packet
    plist->size++;
    
    return (int)plist->size;
}



int pktlist_del(pktlist_t* plist, pkt_t* pkt) {
    pkt_t*  ref;
    pkt_t   copy; 
    
    /// First thing is to free the packet even if it's not in the list
    /// We make a local copy in order to stitch the list back together.
    if (pkt == NULL) {
        return -1;
    }
    ref     = pkt;
    copy    = *pkt;
    if (pkt->buffer != NULL) {
        //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        free(pkt->buffer);
        //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    }
    //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    free(pkt);
    //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
    
    
    /// If there's no plist.  It isn't fatal but it's weird
    if (plist == NULL) {
        return 1;
    }

    /// Downsize the list.  Re-init list if size == 0;
    plist->size--;
    if (plist->size <= 0) {
        pktlist_init(plist);
        return 0;
    }
    
    /// If packet was front of list, move front to next,
    if (plist->front == ref) {
        plist->front = copy.next;
    }
    
    /// If packet was last of list, move last to prev
    if (plist->last == ref) {
        plist->last = copy.prev;
    }
    
    /// Likewise, if the cursor and marker were on the packet, advance them
    if (plist->cursor == pkt) {
        plist->cursor = copy.next;
    }
    if (plist->marker == pkt) {
        plist->marker = copy.next;
    }
    
    /// Stitch the list back together
    if (copy.next != NULL) {
        copy.next->prev = copy.prev;
    }
    if (copy.prev != NULL) {
        copy.prev->next = copy.next;
    }

    return 0;
}



int pktlist_getnew(pktlist_t* plist) {
    INTF_Type intf;
    //time_t      seconds;

    // packet list is not allocated -- that's a serious error
    if (plist == NULL) {
        return -11;
    }
    
    // packet list is empty
    if (plist->cursor == NULL) {
        return -1;
    }
    
    // Save Timestamp 
    plist->cursor->tstamp   = time(NULL);   //;localtime(&seconds);
    
    intf = cliopt_getintf();
    
    // MPipe uses Sequence-ID for message matching
    ///@todo move this into pktlist_add() via sub_mpipe_readframe()
    if (intf == INTF_mpipe) {
        uint16_t    crc_val;
        uint16_t    crc_comp;
    
        plist->cursor->sequence = plist->cursor->buffer[4];
        crc_val                 = (plist->cursor->buffer[0] << 8) + plist->cursor->buffer[1];
        crc_comp                = crc_calc_block(&plist->cursor->buffer[2], plist->cursor->size-2);
        plist->cursor->crcqual  = (crc_comp - crc_val);
    }
    else {
        plist->cursor->crcqual  = 0;
    }

    // return 0 on account that nothing went wrong.  So far, no checks.
    return 0;
}
