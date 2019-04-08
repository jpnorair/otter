/* Copyright 2014, JP Norair
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

// Application Includes
//#include "crc_calc_block.h"
#include "debug.h"
#include "mpipe.h"
#include "otter_app.h"
#include "otter_cfg.h"

// Deprecated
#include "ppipelist.h"

// Local Libraries/Includes
#include <dterm.h>
#include <cJSON.h>
#include <bintex.h>
#include <m2def.h>

// Standard C & POSIX libraries
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>





static dterm_fd_t* mpipe_active_dterm;

static int sub_dtputs(char* str) {
    return dterm_puts(mpipe_active_dterm, str);
}




/** MPipe Threads <BR>
  * ========================================================================<BR>
  * <LI> mpipe_reader() : manages TTY RX, pushes to rlist.  Depends on no other
  *          thread. </LI>
  * <LI> mpipe_writer() : manages TTY TX, gets packets from tlist.  Depends on
  *          dterm_parser() to prepare packet and also waits for mpipe_reader()
  *          to be idle. </LI>
  * <LI> mpipe_parser() : gets packets from rlist, parses the internal 
  *          protocols, writes output to terminal screen, and manages both rlist
  *          and tlist.  Depends on mpipe_reader(), mpipe_writer(), and also
  *          dterm_parser(). </LI>
  */

void* mpipe_reader(void* args) {
/// Thread that:
/// <LI> Listens to mpipe TTY via read(). </LI>
/// <LI> Assembles the packet from TTY data. </LI>
/// <LI> Adds packet into mpipe.rlist, sends cond-sig to mpipe_parser. </LI>
///
    otter_app_t* appdata    = args;
    struct pollfd* fds      = NULL;
    int num_fds;
    int pollcode;
    int ready_fds;
    
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int header_length;
    int payload_length;
    int payload_left;
    int errcode;
    int new_bytes;
    uint8_t syncinput;
    
    if (appdata == NULL) {
        goto mpipe_reader_TERM;
    }
    
    // The Packet lists are used between threads and thus are Mutexed references
    mpipe_handle_t mph = appdata->mpipe;
    
    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    /// Setup for usage of the poll function to flush buffer on read timeouts.
    num_fds = mpipe_pollfd_alloc(mph, &fds, (POLLIN | POLLNVAL | POLLHUP));
    if (num_fds <= 0) {
        fprintf(stderr, "MPipe polling could not be started (error %i): quitting\n", num_fds);
        goto mpipe_reader_TERM;
    }
    
    mpipe_flush(mph, -1, 0, TCIFLUSH);
    
    /// Beginning of read loop
    while (1) {
        errcode = 0;
        
        /// Wait until an FF comes
        ready_fds = poll(fds, num_fds, -1);
        if (ready_fds <= 0) {
            fprintf(stderr, "Polling failure: quitting now\n");
            goto mpipe_reader_TERM;
        }
        
        for (int i=0; i<num_fds; i++) {
            // Handle Errors
            ///@todo change 100ms fixed wait on hangup to a configurable amount
            if (fds[i].revents & (POLLNVAL|POLLHUP)) {
                usleep(100 * 1000);
                errcode = 4;
                goto mpipe_reader_ERR;
            }
        
            // Verify that POLLIN is high.  This should be implicit, but we check explicitly here
            if ((fds[i].revents & POLLIN) == 0) {
                mpipe_flush(mph, i, 0, TCIFLUSH);
                continue;
            }
            
            // Find FF, the first sync byte
            mpipe_reader_SYNC0:
            new_bytes = (int)read(fds[i].fd, &syncinput, 1);
            if (new_bytes < 1) {
                errcode = 1;        // flushable
                goto mpipe_reader_ERR;
            }
            if (syncinput != 0xFF) {
                goto mpipe_reader_SYNC0;
            }
            TTY_PRINTF("TTY: Sync FF Received\n");
            
            // Now wait for a 55, ignoring FFs
            mpipe_reader_SYNC1:
            pollcode = poll(&fds[i], 1, 50);
            if (pollcode <= 0) {
                errcode = 3;        // flushable
                goto mpipe_reader_ERR;
            }
            else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                errcode = 4;        // flushable
                goto mpipe_reader_ERR;
            }
            
            new_bytes = (int)read(fds[i].fd, &syncinput, 1);
            if (new_bytes < 1) {
                errcode = 1;        // flushable
                goto mpipe_reader_ERR;
            }
            if (syncinput == 0xFF) {
                goto mpipe_reader_SYNC1;
            }
            if (syncinput != 0x55) {
                goto mpipe_reader_SYNC0;
            }
            TTY_PRINTF("TTY: Sync 55 Received\n");
            
            // At this point, FF55 was detected.  We get the next 6 bytes of the
            // header, which is the rest of the header.
            /// @todo Make header length dynamic based on control field (last byte).
            ///           However, control field is not yet defined.
            new_bytes       = 0;
            payload_left    = 6;
            rbuf_cursor     = rbuf;
            do {
                pollcode = poll(fds, 1, 50);
                if (pollcode <= 0) {
                    errcode = 3;
                    goto mpipe_reader_ERR;
                }
                else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    errcode = 4;
                    goto mpipe_reader_ERR;
                }
            
                new_bytes       = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
                TTY_PRINTF("header new_bytes = %d\n", new_bytes);
            } while (payload_left > 0);
            
            if (payload_left != 0) {
                continue;
            }
            
            // Bytes 0:1 are the CRC16 checksum.  The controlling app can decide what
            // to do about those.  They are in the buffer.
            // Bytes 2:3 are the Length of the Payload, in big endian.  It can be up
            // to 65535 bytes, but CRC16 won't be great help for more than 250 bytes.
            payload_length  = rbuf[2] * 256;
            payload_length += rbuf[3];
            
            // Byte 4 is a sequence number, which the controlling app can decide what
            // to do with.  Byte 5 is RFU, but in the future it can be used to
            // expand the header.  That logic would go below (currently trivial).
            header_length = 6 + 0;
            
            // Now do some checks to prevent malformed packets.
            if (((unsigned int)payload_length == 0) \
            || ((unsigned int)payload_length > (1024-header_length))) {
                errcode = 2;
                goto mpipe_reader_ERR;
            }
            
            // Receive the remaining payload bytes
            payload_left    = payload_length;
            rbuf_cursor     = &rbuf[6];
            while (payload_left > 0) {
                pollcode = poll(fds, 1, 50);
                if (pollcode <= 0) {
                    errcode = 3;
                    goto mpipe_reader_ERR;
                }
                else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    errcode = 4;
                    goto mpipe_reader_ERR;
                }
            
                new_bytes       = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                // Debugging output
                TTY_PRINTF(stderr, "payload new_bytes = %d\n", new_bytes);
                HEX_DUMP(rbuf_cursor, new_bytes, "read(): ");
                
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
            };

            // Debugging output
            HEX_DUMP(&rbuf[6], payload_length, "pkt   : ");

            // Copy the packet to the rlist and signal mpipe_parser()
            pthread_mutex_lock(appdata->rlist_mutex);
            pktlist_add_rx(&appdata->endpoint, mpipe_intf_get(mph, i), appdata->rlist, rbuf, (size_t)(header_length + payload_length));
            pthread_mutex_unlock(appdata->rlist_mutex);
            
            // Error Handler: wait a few milliseconds, then handle the error.
            /// @todo supply estimated bytes remaining into mpipe_flush()
            mpipe_reader_ERR:
            switch (errcode) {
                case 0: TTY_PRINTF(stderr, "Sending packet rx signal\n");
                        pthread_cond_signal(appdata->pktrx_cond);
                        break;
                
                case 1: TTY_PRINTF(stderr, "MPipe Packet Sync could not be retrieved.\n");
                        mpipe_flush(mph, i, 0, TCIFLUSH);
                        break;
                
                case 2: TTY_PRINTF(stderr, "Mpipe Packet Payload Length is out of bounds.\n");
                        mpipe_flush(mph, i, 0, TCIFLUSH);
                        break;
                    
                case 3: TTY_PRINTF(stderr, "Mpipe Packet RX timed-out\n");
                        mpipe_flush(mph, i, 0, TCIFLUSH);
                        break;
                    
                case 4: if (mpipe_reopen(mph, i) == 0) {
                            mpipe_flush(mph, i, 0, TCIFLUSH);
                            break;
                        }
                        fprintf(stderr, "Connection dropped, quitting now\n");
                        goto mpipe_reader_TERM;
                    
               default: fprintf(stderr, "Unknown error, quitting now\n");
                        goto mpipe_reader_TERM;
            }
        }
    }
    
    mpipe_reader_TERM:
    mpipe_flush(mph, -1, 0, TCIFLUSH);
    
    if (fds != NULL) {
        free(fds);
    }
    
    /// This occurs on uncorrected errors, such as case 4 from above, or other 
    /// unknown errors.
    raise(SIGINT);
    return NULL;
}




void* mpipe_writer(void* args) {
/// Thread that:
/// <LI> Listens for cond-signal from dterm_prompter() indicating that data has
///          been added to mpipe.tlist, via a cond-signal. </LI>
/// <LI> Sends the packet over the TTY. </LI>
///
    otter_app_t* appdata = args;
    
    if (appdata == NULL) {
        goto mpipe_writer_TERM;
    }
    
    while (1) {
        //pthread_mutex_lock(tlist_cond_mutex);
        pthread_cond_wait(appdata->tlist_cond, appdata->tlist_cond_mutex);
        pthread_mutex_lock(appdata->tlist_mutex);
        
        while (appdata->tlist->cursor != NULL) {
            pkt_t* txpkt;
            mpipe_fd_t* intf_fd;
            
            ///@todo wait also for RX to be off
            
            txpkt = appdata->tlist->cursor;
            
            // This is a never-before transmitted packet (not a re-transmit)
            // Move to the next in the list.
            if (appdata->tlist->cursor == appdata->tlist->marker) {
                pkt_t* next_pkt         = appdata->tlist->marker->next;
                appdata->tlist->cursor  = next_pkt;
                appdata->tlist->marker  = next_pkt;
            }
            // This is a packet that has just been re-transmitted.  
            // Move to the marker, which is where the new packets start.
            else {
                appdata->tlist->cursor  = appdata->tlist->marker;
            }
            
            intf_fd = mpipe_fds_resolve(txpkt->intf);
            if (intf_fd != NULL) {
                int bytes_left;
                int bytes_sent;
                uint8_t* cursor;
                
                time(&txpkt->tstamp);
                cursor      = txpkt->buffer;
                bytes_left  = (int)txpkt->size;
                
                // Debugging output
                HEX_DUMP(cursor, bytes_left, "Writing %d bytes to tty\n", bytes_left);
                
                while (bytes_left > 0) {
                    bytes_sent  = (int)write(intf_fd->out, cursor, bytes_left);
                    cursor     += bytes_sent;
                    bytes_left -= bytes_sent;
                }
            }
            
            // We put a an interval between transmissions in order to 
            // facilitate certain blocking implementations of the target MPipe.
            /// @todo make configurable instead of 10ms
            usleep(10000);
        }
        
        pthread_mutex_unlock(appdata->tlist_mutex);
    }
    
    mpipe_writer_TERM:
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_writer() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}



void* mpipe_parser(void* args) {
///@todo wait for mpipe_writer() to complete before killing any tlist data.  
///      A mutex could work here.
///@todo amputate tlist if it gets too big

/// Thread that:
/// <LI> Waits for cond-sig from mpipe_reader() when new packet(s) exist. </LI>
/// <LI> Makes sure the packet is valid. </LI>
/// <LI> If the packet is encrypted, decrypt it. </LI>
/// <LI> If the packet is raw, print it out using Hex formatting. </LI>
/// <LI> If the packet is ALP formatted, do some inspection and attempt to
///          print it out in a human-readable way. </LI>
///
    static char putsbuf[2048];
    otter_app_t* appdata = args;
    dterm_handle_t* dth;
    
    if (appdata == NULL) {
        goto mpipe_parser_TERM;
    }

    dth = appdata->dterm_parent;

    // This looks like an infinite loop, but is not.  The pkt_condition
    // variable will break the loop if the rlist has no new packets.
    // Otherwise it will parse all new packets, one at a time, until there
    // are none remaining.
    while (1) {
        int pkt_condition;  // tracks some error conditions
        pkt_t*  rpkt;
        pkt_t*  tpkt;
    
        //pthread_mutex_lock(pktrx_mutex);
        pthread_cond_wait(appdata->pktrx_cond, appdata->pktrx_mutex);
        pthread_mutex_lock(dth->iso_mutex);
        pthread_mutex_lock(appdata->rlist_mutex);
        pthread_mutex_lock(appdata->tlist_mutex);
        
        mpipe_active_dterm = (dterm_fd_t*)&dth->fd;
        rpkt = appdata->rlist->cursor;
        
        // pktlist_getnew will validate and decrypt the packet:
        // - It returns 0 if all is well
        // - It returns -1 if the list is empty
        // - It returns a positive error code if there is some packet error
        // - rlist->cursor points to the working packet
        pkt_condition = pktlist_getnew(appdata->rlist);
        if (pkt_condition < 0) {
            goto mpipe_parser_END;
        }

        // If packet has an error of some kind -- delete it and move-on.
        // Else, print-out the packet.  This can get rich depending on the
        // internal protocol, and it can result in responses being queued.
        if (pkt_condition > 0) {
            ///@todo some sort of error code
            fprintf(stderr, "A malformed packet was sent for parsing\n");
            pktlist_del(appdata->rlist, rpkt);
        }
        else {
            uint8_t*    payload_front;
            int         payload_bytes;
            bool        clear_rpkt      = true;
            bool        rpkt_is_resp    = false;
            bool        rpkt_is_valid   = false;
            
            // Response Packets should match to a sequence number of the last Request.
            // If there is no match, then there is no response processing, we just 
            // print the damn packet.
            tpkt = appdata->tlist->front;
            while (tpkt != NULL) {
                if (tpkt->sequence == rpkt->sequence) {
                    // matching transmitted packet: it IS a response
                    rpkt_is_resp = true;
                    break;
                }
                tpkt = tpkt->next;
            }
            
            /// If CRC is bad, discard packet now, and rxstat an error
            /// Else, if CRC is good, send packet to processor.
            if (appdata->rlist->cursor->crcqual != 0) {
                ///@todo add rx address of input packet (set to 0)
                dterm_send_rxstat(dth, DFMT_Binary, rpkt->buffer, rpkt->size, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
            }
            else {
                // -----------------------------------------------------------
                ///@todo Here is where decryption might go, if not handled in pktlist
                /// there should be an encryption header at this offset (6),
                /// followed by the payload, and then the real data payload.
                /// The real data payload is followed by a 4 byte Message
                /// Authentication Check (Crypto-MAC) value.  AES128 EAX is the
                /// cryptography and cipher used.
                if (rpkt->buffer[5] & (3<<5)) {
                    // Case with encryption
                    payload_front = &rpkt->buffer[6];
                }
                else {
                    payload_front = &rpkt->buffer[6];
                }
                // -----------------------------------------------------------
                
                // Inspect header to see if M2DEF
                if ((rpkt->buffer[5] & (1<<7)) == 0) {
                    rpkt_is_valid = true;
                    //parse some shit
                    //clear_rpkt = false when there is non-atomic input
                }
                
                // Get Payload Bytes, found in buffer[2:3]
                // Then print-out the payload.
                // If it is a M2DEF payload, the print-out can be formatted in different ways
                payload_bytes   = rpkt->buffer[2] * 256;
                payload_bytes  += rpkt->buffer[3];

                /// 1. If payload is too big, send an error
                /// 2a. If packet is valid, punt it to the ALP output formatter.
                /// 2b. If ALP formatting is correct, send data to the subscriber
                /// 3. If packet is not valid, dump its hex
                ///@note: ALP formatters should deal internally with protocol variations
                if (payload_bytes > rpkt->size) {
                    //dterm_send_error(dth, "rxstat", -1, 0, "Reported Payload Length is larger than buffer");
                }
                else if (rpkt_is_valid) {
                    while (payload_bytes > 0) {
                        size_t putsbytes = 0;
                        uint8_t* lastfront = payload_front;
                        int subsig;
                        int proc_result;
                    
                        /// ALP message:
                        /// proc_result now takes the value from the protocol formatter.
                        /// The formatter will give negative values on framing errors
                        /// and also for protocol errors (i.e. NACKs).
                        proc_result = fmt_fprintalp((uint8_t*)putsbuf, &putsbytes, &payload_front, payload_bytes);
                        
                        /// Successful formatted output gets propagated to any
                        /// subscribers of this ALP ID.
                        subsig = (proc_result >= 0) ? SUBSCR_SIG_OK : SUBSCR_SIG_ERR;
                        subscriber_post(appdata->subscribers, proc_result, subsig, NULL, 0);
                        
                        // Send RXstat message back to control interface.
                        dterm_send_rxstat(dth, DFMT_Native, putsbuf, putsbytes, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
                        
                        // Recalculate message size following the treatment of the last segment
                        payload_bytes -= (payload_front - lastfront);
                    }
                }
                else {
                    size_t putsbytes = 0;
                    fmt_printhex((uint8_t*)putsbuf, &putsbytes, &payload_front, payload_bytes, 16);
                    dterm_send_rxstat(dth, DFMT_Text, putsbuf, putsbytes, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
                }
            }
            
            // Clear the rpkt if required
            if (clear_rpkt) {
                pktlist_del(appdata->rlist, rpkt);
            }
            
            // Clear the tpkt if it is matched with an rpkt
            // Also, clear the oldest tpkt if it's timestamp is of a certain amout.
            ///@todo Change timestamp so that it is not hardcoded
            if (rpkt_is_resp == false) {
                ///@todo check if old
                //if (rlist->front->tstamp == 0) {    
                //    tpkt = tlist->front;
                //    ---> do routine in else if below
                //}
            }
            else if (tpkt != NULL) {
                pktlist_del(appdata->tlist, tpkt);
            }
        } 
        
        mpipe_parser_END:
        pthread_mutex_unlock(appdata->tlist_mutex);
        pthread_mutex_unlock(appdata->rlist_mutex);
        pthread_mutex_unlock(dth->iso_mutex);
        
        ///@todo Can check for major error in pkt_condition
        ///      Major errors are integers less than -1
        
    } // END OF WHILE()
    
    mpipe_parser_TERM:
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_parser() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}

