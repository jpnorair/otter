//
//  pktlist.c
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "pktlist.h"
#include "cliopt.h"

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



void sub_writeheader_null(pkt_t* newpkt, uint8_t* data, size_t datalen) {
    memcpy(&newpkt->buffer[0], data, datalen);
}


void sub_writeheader_modbus(pkt_t* newpkt, uint8_t* data, size_t datalen) {
/// Adds 1, 3, or 10 bytes to packet depending on conditions
    int user_id;
    int hdr_size;
    
    /// Basic Modbus just puts the destination address.
    newpkt->buffer[0]   = cliopt_getdstaddr() & 0xFF;
    hdr_size            = 1;
    
    /// Enhanced Modbus uses commands 68, 69, 70.
    /// MPipe/ALP encapsulation uses these commands.
    /// We know it's enhanced modbus if first byte is 0xC0/D0, which is the ALP
    /// Specifier as well as an illegal Function ID in regular modbus.
    if ((data[0] == 0xC0) || (data[0] == 0xD0)) {
        user_id             = cliopt_getuser();
        newpkt->buffer[1]   = 68 + (user_id & 3);
        newpkt->buffer[2]   = cliopt_getsrcaddr() & 0xFF;
        hdr_size            = 3;
    
        if (user_id < 2) {   
            uint32_t epoch = (uint32_t)time(NULL);
            
            // 56 bit nonce
            // bytes 3:6 are 32bit epoch
            // bytes 7:9 are sequence number
            newpkt->buffer[3] = (epoch >> 24) & 0xFF;
            newpkt->buffer[4] = (epoch >> 16) & 0xFF;
            newpkt->buffer[5] = (epoch >> 8) & 0xFF;
            newpkt->buffer[6] = (epoch >> 0) & 0xFF;
            newpkt->buffer[7] = (newpkt->sequence >> 16) & 0xFF;
            newpkt->buffer[8] = (newpkt->sequence >> 8) & 0xFF;
            newpkt->buffer[9] = (newpkt->sequence >> 0) & 0xFF;
            
            ///@todo do encryption on payload
            
            hdr_size = 10;
        }
    }
    
    // need to add header length to size value
    newpkt->size += hdr_size;
    
    memcpy(&newpkt->buffer[hdr_size], data, datalen);
}


void sub_writeheader_mpipe(pkt_t* newpkt, uint8_t* data, size_t datalen) {
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


void sub_writefooter_null(pkt_t* newpkt) {
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
    void (*put_header)(pkt_t*, uint8_t*, size_t);
    void (*put_footer)(pkt_t*);
    
    if (plist == NULL) {
        return -1;
    }
    
    newpkt = malloc(sizeof(pkt_t));
    if (newpkt == NULL) {
        return -2;
    }
    
    // Offset is dependent if we are writing a header (8 bytes) or not.
    if (write_header) {
        switch (cliopt_getintf()) {
            case INTF_mpipe:    
                max_overhead= 8;
                put_header  = &sub_writeheader_mpipe;
                put_footer  = &sub_writefooter_mpipe;
                break;
            
            case INTF_modbus:
                max_overhead= 12;
                put_header  = &sub_writeheader_modbus;
                put_footer  = &sub_writefooter_modbus;
                break;
            
            default:
                goto pktlist_add_SETNULL;
        }
    }
    else {
    pktlist_add_SETNULL:
        max_overhead= 0;
        put_header  = &sub_writeheader_null;
        put_footer  = &sub_writefooter_null;
    }

    // Setup list connections for the new packet
    // Also allocate the buffer of the new packet
    ///@todo Change the hardcoded +8 to a dynamic detection of the header
    ///      length, which depends on current mode settings in the "cli".
    ///      Dynamic header isn't implemented yet, so no rush.
    newpkt->prev    = plist->last;
    newpkt->next    = NULL;
    newpkt->size    = size;
    newpkt->buffer  = malloc(size + max_overhead);
    if (newpkt->buffer == NULL) {
        return -3;
    }
    
    // Save timestamp: this may or may not get used, but it's saved anyway.
    // The default sequence (which is available to header generation) is
    // from the rotating nonce of the plist.
    newpkt->tstamp      = time(NULL);
    newpkt->sequence    = plist->txnonce++;
    
    // If "write_header" is set, then we need to write our own header (e.g. for
    // TX'ing).  If not, we copy the data directly.
    put_header(newpkt, data, size);        ///@todo Set Control Field here based on Cli (instead of NULL)

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
    if (intf == INTF_mpipe) {
        uint16_t    crc_val;
        uint16_t    crc_comp;
    
        plist->cursor->sequence = plist->cursor->buffer[4];
        crc_val                 = (plist->cursor->buffer[0] << 8) + plist->cursor->buffer[1];
        crc_comp                = crc_calc_block(&plist->cursor->buffer[2], plist->cursor->size-2);
        plist->cursor->crcqual  = (crc_comp - crc_val);
    }
    else if (intf == INTF_modbus) {
        plist->cursor->crcqual  = mbcrc_calc_block(&plist->cursor->buffer[0], plist->cursor->size);
    }
    else {
        plist->cursor->crcqual  = 0;
    }

    // return 0 on account that nothing went wrong.  So far, no checks.
    return 0;
}
