//
//  pktlist.c
//  otter
//
//  Created by John Peter Norair on 18/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "pktlist.h"

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
    
    return 0;
}




int pktlist_add(pktlist_t* plist, bool write_header, uint8_t* data, size_t size) {
    pkt_t* newpkt;
    size_t offset;
    
    if (plist == NULL) {
        return -1;
    }
    
    newpkt = malloc(sizeof(pkt_t));
    if (newpkt == NULL) {
        return -2;
    }
    
    // Offset is dependent if we are writing a header (8 bytes) or not.
    offset = (write_header) ? 8 : 0;
    
    // Setup list connections for the new packet
    // Also allocate the buffer of the new packet
    ///@todo Change the hardcoded +8 to a dynamic detection of the header
    ///      length, which depends on current mode settings in the "cli".
    ///      Dynamic header isn't implemented yet, so no rush.
    newpkt->prev    = plist->last;
    newpkt->next    = NULL;
    newpkt->size    = size + offset;
    newpkt->buffer  = malloc(newpkt->size);
    if (newpkt->buffer == NULL) {
        return -3;
    }
    
    // Copy Payload into Packet buffer
    // If "write_header" is set, then we need to write our own header (e.g. for
    // TX'ing).  If not, we copy the data directly.
    memcpy(&newpkt->buffer[offset], data, newpkt->size);
    if (write_header) {
        newpkt->buffer[0]   = 0xff;
        newpkt->buffer[1]   = 0x55;
        newpkt->buffer[2]   = 0;
        newpkt->buffer[3]   = 0;
        newpkt->buffer[4]   = (size >> 8) & 0xff;
        newpkt->buffer[5]   = size & 0xff;
        newpkt->buffer[6]   = 0;
        newpkt->buffer[7]   = 0;            ///@todo Set Control Field here based on Cli.
    }

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
    if (write_header) {
        uint16_t crcval;
        newpkt->buffer[6]   = newpkt->sequence;
        crcval              = crc_calc_block(&newpkt->buffer[4], newpkt->size - 4);
        newpkt->buffer[2]   = (crcval >> 8) & 0xff;
        newpkt->buffer[3]   = crcval & 0xff;
    }
    
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
    uint16_t    crc_val;
    uint16_t    crc_comp;
    //time_t      seconds;

    // packet list is not allocated -- that's a serious error
    if (plist == NULL) {
        return -11;
    }
    
    // packet list is empty
    if (plist->cursor == NULL) {
        return -1;
    }
    
    // Save Timestamp and Sequence ID parameters
    plist->cursor->tstamp   = time(NULL);   //;localtime(&seconds);
    plist->cursor->sequence = plist->cursor->buffer[4];
    
    // Determine CRC quality of the received packet
    crc_val                 = (plist->cursor->buffer[0] << 8) + plist->cursor->buffer[1];
    crc_comp                = crc_calc_block(&plist->cursor->buffer[2], plist->cursor->size-2);
    plist->cursor->crcqual  = (crc_comp - crc_val);
    
    // return 0 on account that nothing went wrong.  So far, no checks.
    return 0;
}
