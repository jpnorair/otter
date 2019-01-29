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
#include "ppipelist.h"
#include "otter_cfg.h"

// Local Libraries/Includes
#include <dterm.h>
#include <cJSON.h>
#include <bintex.h>
#include "m2def.h"

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





static dterm_t* mpipe_active_dterm;

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
    struct pollfd* fds = NULL;
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
    
    
    // The Packet lists are used between threads and thus are Mutexed references
    mpipe_arg_t* mparg = (mpipe_arg_t*)args;
    mpipe_handle_t mph = mparg->handle;
    
    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    /// Setup for usage of the poll function to flush buffer on read timeouts.
    num_fds = mpipe_pollfd_alloc(mph, fds, (POLLIN | POLLNVAL | POLLHUP));
    if (num_fds <= 0) {
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
            pthread_mutex_lock(mparg->rlist_mutex);
            pktlist_add_rx(mparg->endpoint, mpipe_intf_get(mph, i), mparg->rlist, rbuf, (size_t)(header_length + payload_length));
            pthread_mutex_unlock(mparg->rlist_mutex);
            
            // Error Handler: wait a few milliseconds, then handle the error.
            /// @todo supply estimated bytes remaining into mpipe_flush()
            mpipe_reader_ERR:
            switch (errcode) {
                case 0: TTY_PRINTF(stderr, "Sending packet rx signal\n");
                        pthread_cond_signal(mparg->pktrx_cond);
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
    mpipe_arg_t* mparg = (mpipe_arg_t*)args;
    
    while (1) {
        
        //pthread_mutex_lock(tlist_cond_mutex);
        pthread_cond_wait(mparg->tlist_cond, mparg->tlist_cond_mutex);
        pthread_mutex_lock(mparg->tlist_mutex);
        
        while (mparg->tlist->cursor != NULL) {
            pkt_t* txpkt;
            mpipe_fd_t* intf_fd;
            
            ///@todo wait also for RX to be off
            
            txpkt = mparg->tlist->cursor;
            
            // This is a never-before transmitted packet (not a re-transmit)
            // Move to the next in the list.
            if (mparg->tlist->cursor == mparg->tlist->marker) {
                pkt_t* next_pkt         = mparg->tlist->marker->next;
                mparg->tlist->cursor    = next_pkt;
                mparg->tlist->marker    = next_pkt;
            }
            // This is a packet that has just been re-transmitted.  
            // Move to the marker, which is where the new packets start.
            else {
                mparg->tlist->cursor    = mparg->tlist->marker;
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
        
        pthread_mutex_unlock(mparg->tlist_mutex);
    }
    
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
    mpipe_arg_t* mparg = (mpipe_arg_t*)args;

    // This looks like an infinite loop, but is not.  The pkt_condition
    // variable will break the loop if the rlist has no new packets.
    // Otherwise it will parse all new packets, one at a time, until there
    // are none remaining.
    while (1) {
        int pkt_condition;  // tracks some error conditions
    
        //pthread_mutex_lock(pktrx_mutex);
        pthread_cond_wait(mparg->pktrx_cond, mparg->pktrx_mutex);
        pthread_mutex_lock(mparg->dtwrite_mutex);
        pthread_mutex_lock(mparg->rlist_mutex);
        pthread_mutex_lock(mparg->tlist_mutex);
        
        mpipe_active_dterm = (dterm_t*)mparg->dtprint;
        
        // pktlist_getnew will validate and decrypt the packet:
        // - It returns 0 if all is well
        // - It returns -1 if the list is empty
        // - It returns a positive error code if there is some packet error
        // - rlist->cursor points to the working packet
        pkt_condition = pktlist_getnew(mparg->rlist);
        if (pkt_condition < 0) {
            goto mpipe_parser_END;
        }

        // If packet has an error of some kind -- delete it and move-on.
        // Else, print-out the packet.  This can get rich depending on the
        // internal protocol, and it can result in responses being queued.
        if (pkt_condition > 0) {
            ///@todo some sort of error code
            fprintf(stderr, "A malformed packet was sent for parsing\n");
            pktlist_del(mparg->rlist, mparg->rlist->cursor);
        }
        else {
            pkt_t*      tpkt;
            uint8_t*    payload_front;
            size_t      payload_bytes;
            bool        clear_rpkt      = true;
            bool        rpkt_is_resp    = false;
            bool        rpkt_is_valid   = false;
            
            // Response Packets should match to a sequence number of the last Request.
            // If there is no match, then there is no response processing, we just 
            // print the damn packet.
            tpkt = mparg->tlist->front;
            while (tpkt != NULL) {
                if (tpkt->sequence == mparg->rlist->cursor->sequence) {
                    // matching transmitted packet: it IS a response
                    rpkt_is_resp = true;
                    break;
                }
                tpkt = tpkt->next;
            }
            
            // If Verbose, Print received header in real language
            // If not Verbose, just print the encoded packet status
            if (cliopt_isverbose()) {
                sprintf(putsbuf, "\n" _E_BBLK "RX'ed %zu bytes at %s, %s CRC: %s" _E_NRM "\n",
                            mparg->rlist->cursor->size,
                            fmt_time(&mparg->rlist->cursor->tstamp),
                            fmt_crc(mparg->rlist->cursor->crcqual),
                            fmt_hexdump_header(mparg->rlist->cursor->buffer)
                        );
            }
            else {
                switch (cliopt_getformat()) {
                    case FORMAT_Hex: {
                        putsbuf[0] = '0';
                        putsbuf[1] = '0' + (mparg->rlist->cursor->crcqual != 0);
                        putsbuf[2] = 0;
                        ///@todo put this to the buffer without flushing it
                    } break;
                    case FORMAT_Json: ///@todo
                    case FORMAT_Bintex: ///@todo
                    default: {
                        const char* valid_sym = _E_GRN"v";
                        const char* error_sym = _E_RED"x"; 
                        const char* crc_sym   = (mparg->rlist->cursor->crcqual == 0) ? valid_sym : error_sym;
                        sprintf(putsbuf, _E_WHT "[" "%s" _E_WHT "][%03d] " _E_NRM, 
                                crc_sym, mparg->rlist->cursor->sequence);
                    } break;
                }
            }
            sub_dtputs(putsbuf);
            
            /// If CRC is bad, dump hex of buffer-size and discard the 
            /// packet now.
            ///@note in present versions there is some unreliability with
            ///      CRC: possibly data alignment or something, or might be
            ///      a problem on the test sender.
            if (mparg->rlist->cursor->crcqual != 0) {
                fmt_printhex(&sub_dtputs, &mparg->rlist->cursor->buffer[6], mparg->rlist->cursor->size-6, 16);
                pktlist_del(mparg->rlist, mparg->rlist->cursor);
                goto mpipe_parser_END;
            }
            
            // Here is where decryption would go
            if (mparg->rlist->cursor->buffer[5] & (3<<5)) {
                ///@todo Deal with encryption here.  When implemented, there
                /// should be an encryption header at this offset (6), 
                /// followed by the payload, and then the real data payload.
                /// The real data payload is followed by a 4 byte Message
                /// Authentication Check (Crypto-MAC) value.  AES128 EAX
                /// is the cryptography and cipher used.
                payload_front = &mparg->rlist->cursor->buffer[6];
            }
            else {
                payload_front = &mparg->rlist->cursor->buffer[6];
            }
            
            // Inspect header to see if M2DEF
            if ((mparg->rlist->cursor->buffer[5] & (1<<7)) == 0) {
                rpkt_is_valid = true;
                //parse some shit
                //clear_rpkt = false when there is non-atomic input
            }
            
            // Get Payload Bytes, found in buffer[2:3]
            // Then print-out the payload.
            // If it is a M2DEF payload, the print-out can be formatted in different ways
            payload_bytes   = mparg->rlist->cursor->buffer[2] * 256;
            payload_bytes  += mparg->rlist->cursor->buffer[3];

            // Send an error if payload bytes is too big
            if (payload_bytes > mparg->rlist->cursor->size) {
                ///@todo handle format variations
                sprintf(putsbuf, "... Reported Payload Length (%zu) is larger than buffer (%zu).\n" \
                                 "... Possible transmission error.\n",
                                payload_bytes, mparg->rlist->cursor->size
                        );
                sub_dtputs(putsbuf);
            }
            
            ///@note: ALP handlers should deal internally with format variations
            else if (rpkt_is_valid)  {
                //m2def_sprintf(putsbuf, &rlist->cursor->buffer[6], 2048, "");
                //sub_dtputs(putsbuf)
                fmt_fprintalp(&sub_dtputs, mparg->msgcall, payload_front, payload_bytes);
            }
            
            /// Dump hex if not using ALP
            ///@todo handle format variations
            else {
                fmt_printhex(&sub_dtputs, payload_front, payload_bytes, 16);
            }

            // Clear the rpkt if required, and move the cursor to the next
            if (clear_rpkt) {
                pkt_t*  scratch = mparg->rlist->cursor;
                //rlist->cursor   = rlist->cursor->next;
                pktlist_del(mparg->rlist, scratch);
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
                pktlist_del(mparg->tlist, tpkt);
            }
        } 
        
        mpipe_parser_END:
        pthread_mutex_unlock(mparg->tlist_mutex);
        pthread_mutex_unlock(mparg->rlist_mutex);
        pthread_mutex_unlock(mparg->dtwrite_mutex);
        
        ///@todo Can check for major error in pkt_condition
        ///      Major errors are integers less than -1
        
    } // END OF WHILE()
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_parser() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}

