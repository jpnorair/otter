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
#include <errno.h>





static int64_t timespec_diffms(struct timespec start, struct timespec end) {
    int64_t result;

    result  = (int64_t)((end.tv_sec * 1000) + (end.tv_nsec / 1000000)) \
            - (int64_t)((start.tv_sec * 1000) + (start.tv_nsec / 1000000));
 
    return result;
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
    mpipe_handle_t mph      = NULL;
    int num_fds;
    int pollcode;
    int polltimeout;
    int ready_fds;
    
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int header_length;
    int payload_length;
    int payload_left;
    int errcode;
    int new_bytes;
    uint8_t syncinput;
    int frame_length;
    int i = 0;
    
    if (appdata == NULL) {
        goto mpipe_reader_TERM;
    }
    
    // The Packet lists are used between threads and thus are Mutexed references
    mph = appdata->mpipe;
    
    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    /// Setup for usage of the poll function to flush buffer on read timeouts.
    num_fds = mpipe_pollfd_alloc(mph, &fds, (POLLIN | POLLNVAL | POLLHUP));
    if (num_fds <= 0) {
        ERR_PRINTF("MPipe polling could not be started (error %i): quitting\n", num_fds);
        goto mpipe_reader_TERM;
    }
    
    // polltimeout starts as -1, and it is assigned ever longer timeouts until
    // all devices are reconnected
    polltimeout = -1;
    
    mpipe_flush(mph, -1, 0, MPIFLUSH);
    
    /// Beginning of read loop
    while (1) {
        errcode = 0;
        
#       if (OTTER_FEATURE_NOPOLL == ENABLED)
        {   int unused_bytes;
            struct timespec ref;
            struct timespec test;
            
            mpipe_reader_INIT:
            rbuf_cursor     = rbuf;
            unused_bytes    = 0;
            payload_left    = 1;
            
            while (1) {
                /// Check size of bytes in the hopper, and compare with payload_left (requested bytes)
                /// Only do read() if we need more bytes
                
                // Read from interface, and time how long it takes to return
                if (clock_gettime(CLOCK_MONOTONIC, &ref) != 0) {
                    errcode = -1;
                    goto mpipe_reader_ERR;
                }
                new_bytes = (int)read(fds[0].fd, rbuf_cursor, payload_left);
                //HEX_DUMP(rbuf_cursor, new_bytes, "read(): ");
                if (new_bytes <= 0) {
                    errcode = 5 - (new_bytes == 0);
                    goto mpipe_reader_ERR;
                }
                if (clock_gettime(CLOCK_MONOTONIC, &test) != 0) {
                    errcode = -1;
                    goto mpipe_reader_ERR;
                }
                
                // If read took longer than 50ms (should be configurable ms),
                // we need to restart the reception state machine.
                if (timespec_diffms(ref, test) > 50) {
                    errcode = 0;
                    for (; (new_bytes>0)&&(*rbuf_cursor!=0xFF); new_bytes--, rbuf_cursor++);
                    if (new_bytes == 0) {
                        goto mpipe_reader_INIT;
                    }
                    unused_bytes = new_bytes-1;
                    new_bytes = 1;
                }
                else {
                    unused_bytes = 0;
                }
                
                mpipe_reader_STATEHANDLER:
                syncinput       = *rbuf_cursor;
                rbuf_cursor    += new_bytes;
                unused_bytes   -= new_bytes;
                payload_left   -= new_bytes;
                
                switch (errcode) {
                case 0: payload_left = 1;
                        if (syncinput != 0xFF) {
                            break;
                        }
                        errcode = 1;
                        break;
                
                case 1: payload_left = 1;
                        if (syncinput == 0xFF) {
                            break;
                        }
                        if (syncinput != 0x55) {
                            errcode = 0;
                            break;
                        }
                        // Sync-Found: realign rbuf if necessary
                        for (int j=0; j<unused_bytes; j++) {
                            rbuf[j] = rbuf_cursor[j];
                        }
                        rbuf_cursor = rbuf;
                        errcode = 2;
                        payload_left = 6;
                        break;
                
                // Header (6 bytes)
                case 2: if (payload_left > 0) {
                            break;
                        }
                        errcode = 3;
                        payload_length  = rbuf[2] * 256;
                        payload_length += rbuf[3];
                        payload_left    = payload_length;
                        header_length   = 6 + 0;
                        if ((payload_length == 0) || (payload_length > (1024-header_length))) {
                            errcode = 2;
                            goto mpipe_reader_ERR;
                        }
                        break;
                
                // Payload (N bytes)
                case 3: if (payload_left <= 0) {
                            errcode = 0;
                            goto mpipe_reader_READDONE;
                        }
                        break;
                
               default: rbuf_cursor = rbuf;
                        payload_left = 1;
                        errcode = 0;
                        unused_bytes = 0;
                        break;
                }
                
                if (unused_bytes > 0) {
                    new_bytes = (payload_left < unused_bytes) ? payload_left : unused_bytes;
                    goto mpipe_reader_STATEHANDLER;
                }
            }

#       else // NORMAL MODE
        // Handle timeout (return 0).
        // Timeouts only occur when there is a job to reconnect to some lost
        // connections.
        ready_fds = poll(fds, num_fds, polltimeout);
        if (ready_fds == 0) {
            int num_dc = 0;
            int connfail;
            
            for (i=0; i<num_fds; i++) {
                if (fds[i].fd < 0) {
                    VERBOSE_PRINTF("Attempting to reconnect on %s\n", mpipe_file_get(mph, i));
                    connfail = (mpipe_reopen(mph, i) != 0);
                    num_dc  += connfail;
                    if (connfail == 0) {
                        fds[i].fd = ((mpipe_tab_t*)mph)->intf[i].fd.in;
                        fds[i].events = (POLLIN | POLLNVAL | POLLHUP);
                    }
                }
            }
            if (num_dc == 0) {
                polltimeout = -1;
            }
            else if (polltimeout >= 30000) {
                polltimeout = 60000;
            }
            else {
                polltimeout *= 2;
            }
            continue;
        }
        
        // Handle fatal errors
        if (ready_fds < 0) {
            ERR_PRINTF("Polling failure in %s, line %i\n", __FUNCTION__, __LINE__);
            goto mpipe_reader_TERM;
        }
        
        for (i=0; i<num_fds; i++) {
            // Handle Errors
            ///@todo change 100ms fixed wait on hangup to a configurable amount
            if (fds[i].revents & (POLLNVAL|POLLHUP)) {
                usleep(100 * 1000);
                errcode = 5;
                goto mpipe_reader_ERR;
            }
        
            // Verify that POLLIN is high.  This should be implicit, but we check explicitly here
            if ((fds[i].revents & POLLIN) == 0) {
                mpipe_flush(mph, i, 0, MPIFLUSH);
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
            TTY_PRINTF("Sync FF Received\n");
            
            // Now wait for a 55, ignoring FFs
            mpipe_reader_SYNC1:
            pollcode = poll(&fds[i], 1, 50);
            if (pollcode <= 0) {
                errcode = 4;        // flushable
                goto mpipe_reader_ERR;
            }
            else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                errcode = 5;        // flushable
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
            TTY_PRINTF("Sync 55 Received\n");
            
            // At this point, FF55 was detected.  We get the next 6 bytes of the
            // header, which is the rest of the header.
            /// @todo Make header length dynamic based on control field (last byte).
            ///           However, control field is not yet defined.
            new_bytes       = 0;
            payload_left    = 6;
            rbuf_cursor     = rbuf;
            while (payload_left > 0) {
                pollcode = poll(&fds[i], 1, 50);
                if (pollcode <= 0) {
                    errcode = 4;
                    goto mpipe_reader_ERR;
                }
                else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    errcode = 5;
                    goto mpipe_reader_ERR;
                }
            
                new_bytes       = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
                TTY_PRINTF("header new_bytes = %d\n", new_bytes);
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
                pollcode = poll(&fds[i], 1, 50);
                if (pollcode <= 0) {
                    errcode = 4;
                    goto mpipe_reader_ERR;
                }
                else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                    errcode = 5;
                    goto mpipe_reader_ERR;
                }
            
                new_bytes       = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                // Debugging output
                TTY_PRINTF("payload new_bytes = %d\n", new_bytes);
                HEX_DUMP(rbuf_cursor, new_bytes, "read(): ");
                
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
            }

#       endif

            mpipe_reader_READDONE:

            // Debugging output
            HEX_DUMP(&rbuf[6], payload_length, "pkt   : ");

            // Copy the packet to the rlist and signal mpipe_parser()
            if (pktlist_add_rx(&appdata->endpoint, mpipe_intf_get(mph, i), appdata->rlist, rbuf, (size_t)(header_length + payload_length)) == NULL) {
                errcode = 3;
            }
            
            // Error Handler: wait a few milliseconds, then handle the error.
            /// @todo supply estimated bytes remaining into mpipe_flush()
            mpipe_reader_ERR:

            switch (errcode) {
            case 0: TTY_RX_PRINTF("Packet Received Successfully (%d bytes).\n", frame_length);
                    if (pthread_mutex_trylock(appdata->pktrx_mutex) == 0) {
                        appdata->pktrx_cond_inactive = false;
                        pthread_cond_signal(appdata->pktrx_cond);
                        pthread_mutex_unlock(appdata->pktrx_mutex);
                    }
                    break;
            
            case 1: TTY_RX_PRINTF("MPipe Packet Sync could not be retrieved.\n");
                    goto mpipe_reader_ERRFLUSH;
            
            case 2: TTY_RX_PRINTF("Mpipe Packet Payload Length (%d bytes) is out of bounds.\n", frame_length);
                    goto mpipe_reader_ERRFLUSH;
            
            case 3: TTY_RX_PRINTF("Mpipe Packet frame has invalid data (bad crypto or CRC).\n");
                    goto mpipe_reader_ERRFLUSH;
            
            case 4: TTY_RX_PRINTF("Mpipe Packet RX timed-out\n");
            mpipe_reader_ERRFLUSH:
                    mpipe_flush(mph, i, 0, MPIFLUSH);
                    break;
                
            case 5: if (mpipe_reopen(mph, i) == 0) {
                        mpipe_flush(mph, i, 0, MPIFLUSH);
                    }
                    else {
                        VERBOSE_PRINTF("Connection dropped on %s: queuing for reconnect\n", mpipe_file_get(mph, i));
                        ///@todo initial polltimeout should be an environment variable
                        polltimeout = 4000;
                    }
                    break;
                
            default: ERR_PRINTF("Fatal error in %s: Quitting\n", __FUNCTION__);
                    goto mpipe_reader_TERM;
            }
        }
    }
    
    mpipe_reader_TERM:
    mpipe_flush(mph, -1, 0, MPIOFLUSH);
    
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
    pkt_t* txpkt;
    mpipe_handle_t mph;
    int id_i;
    
    if (appdata == NULL) {
        goto mpipe_writer_TERM;
    }

    mph = appdata->mpipe;

    while (1) {
        pthread_mutex_lock(appdata->tlist_cond_mutex);
        appdata->tlist_cond_inactive = true;
        while (appdata->tlist_cond_inactive) {
            pthread_cond_wait(appdata->tlist_cond, appdata->tlist_cond_mutex);
        }

        while (1) {
            txpkt = pktlist_get(appdata->tlist);
            if (txpkt == NULL) {
                break;
            }

            if (txpkt->intf == NULL) {
                id_i = (int)mpipe_numintf_get(mph);
                while (id_i >= 0) {
                    id_i--;
                    mpipe_writeto_intf(mpipe_intf_get(mph, id_i), txpkt->buffer, (int)txpkt->size);
                }
            }
            else {
                id_i = mpipe_id_resolve(mph, txpkt->intf);
                mpipe_writeto_intf(txpkt->intf, txpkt->buffer, (int)txpkt->size);
            }

            //dterm_publish_txstat(dth, DFMT_Native, txpkt->buffer, txpkt->size, 0, txpkt->sequence, txpkt->tstamp);

            ///@note This call to mpipe_flush will block until all the bytes
            /// are transmitted.  In the special case of txpkt->intf == NULL,
            /// it will block until all bytes on all interfaces are transmitted
            /// as long as all interfaces have same baud rate
            mpipe_flush(mph, id_i, (int)txpkt->size, MPODRAIN);

            ///@todo this deletion should be replaced with punt & sequence 
            ///      delete, but that is not always working properly.
#           ifndef _PUNT_AND_PURGE
            pktlist_del(txpkt);
#           endif
        }

        pthread_mutex_unlock(appdata->tlist_cond_mutex);
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
    if (dth->ext != appdata) {
        ERR_PRINTF("Error: dterm handle is not linked to dterm_parent in application data.\n");
        goto mpipe_parser_TERM;
    }

    // This looks like an infinite loop, but is not.  The pkt_condition
    // variable will break the loop if the rlist has no new packets.
    // Otherwise it will parse all new packets, one at a time, until there
    // are none remaining.
    while (1) {
        int pkt_condition;  // tracks some error conditions
        pkt_t*  rpkt;
    
        pthread_mutex_lock(appdata->pktrx_mutex);
        appdata->pktrx_cond_inactive = true;
        while (appdata->pktrx_cond_inactive) {
            pthread_cond_wait(appdata->pktrx_cond, appdata->pktrx_mutex);
        }
        pthread_mutex_lock(dth->iso_mutex);
        pthread_mutex_unlock(appdata->pktrx_mutex);
        
        /// pktlist_parse will validate the packet with CRC:
        /// - It returns 0 if all is well
        /// - It returns -1 if the list is empty
        /// - It returns a positive error code if there is some packet error
        /// - rlist->cursor points to the working packet
        while (1) {
            uint8_t*    payload_front;
            int         payload_bytes;
            uint64_t    rxaddr;
            bool        rpkt_is_valid   = false;
            
            rpkt = pktlist_parse(&pkt_condition, appdata->rlist);
            if (pkt_condition < 0) {
                break;
            }
            
            /// If packet has an error of some kind -- delete it and move-on.
            /// Else, print-out the packet.  This can get rich depending on the
            /// internal protocol, and it can result in responses being queued.
            if (pkt_condition > 0) {
                ///@todo some sort of error code
                ERR_PRINTF("A malformed packet was sent for parsing\n");
                dterm_publish_rxstat(dth, DFMT_Binary, rpkt->buffer, rpkt->size, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
                
                pktlist_del(rpkt);
                
                ///@todo this packet punting model isn't yet functioning for
                ///      mpipe.  Currently, TX packet is simply deleted after
                ///      it is sent. 
#               ifdef _PUNT_AND_PURGE
                if (appdata->tlist->front != appdata->tlist->cursor) {
                    pktlist_punt(appdata->tlist->front);
                }
#               endif
                break;
            }

            ///@todo packet punt and delete on sequence match is not working
            ///      properly yet.
            /// Response Packets should match to a sequence number of the last Request.
            /// If there is no match, then nothing in tlist is deleted.
            ///      it is sent. 
#           ifdef _PUNT_AND_PURGE
            pktlist_del_sequence(appdata->tlist, rpkt->sequence);
#           endif
            
            /// For Mpipe, the address is implicit based on the interface vid
            ///@todo make sure this works properly.
            rxaddr = devtab_get_uid(appdata->endpoint.devtab, rpkt->intf);
            
            // Get Payload Bytes, found in buffer[2:3]
            // Then print-out the payload.
            // If it is a M2DEF payload, the print-out can be formatted in different ways
            payload_bytes   = rpkt->buffer[2] * 256;
            payload_bytes  += rpkt->buffer[3];
            
            // Inspect header to see if M2DEF
            if ((rpkt->buffer[5] & (1<<7)) == 0) {
                rpkt_is_valid = true;

                ///@todo consider any need to deal with fragmentation.  Maybe
                /// via subscriber module, but a secondary buffer required.
            }
            
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
            
            /// - If packet is valid and framing correct, process packet.
            /// - Else, dump the hex
            if ((rpkt_is_valid) && (payload_bytes <= rpkt->size)) {
                while (payload_bytes > 0) {
                    size_t putsbytes    = 0;
                    uint8_t* lastfront  = payload_front;
                    int subsig;
                    int proc_result;

                    /// ALP message:
                    /// proc_result now takes the value from the protocol formatter.
                    /// The formatter will give negative values on framing errors
                    /// and also for protocol errors (i.e. NACKs).
                    proc_result = fmt_fprintalp((uint8_t*)putsbuf, &putsbytes, &payload_front, payload_bytes);
                    
                    ///@todo this is a temporary additive to log log data
                    if (proc_result == 4) {
                        dterm_send_log(dth, putsbuf, putsbytes);
                    }

                    /// Successful formatted output gets propagated to any
                    /// subscribers of this ALP ID.
                    subsig = (proc_result >= 0) ? SUBSCR_SIG_OK : SUBSCR_SIG_ERR;
                    subscriber_post(appdata->subscribers, proc_result, subsig, NULL, 0);
                    
                    // Send RXstat message back to control interface.
                    dterm_publish_rxstat(dth, DFMT_Native, putsbuf, putsbytes, rxaddr, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
                    
                    // Recalculate message size following the treatment of the last segment
                    payload_bytes -= (payload_front - lastfront);
                }
            }
            else {
                size_t putsbytes = 0;
                ///@todo better way to send an error via dterm_publish_rxstat()
                fmt_printhex((uint8_t*)putsbuf, &putsbytes, &payload_front, rpkt->size, 16);
                dterm_publish_rxstat(dth, DFMT_Text, putsbuf, putsbytes, rxaddr, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
            }
            
            // Clear the rpkt
            pktlist_del(rpkt);
        } 
        
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


/*
#       elif 0
        {
            mpipe_reader_SYNC0:
            syncinput = 0;
            new_bytes = (int)read(fds[i].fd, &syncinput, 1);
            if (new_bytes < 1) {
                usleep(10 * 1000);
                goto mpipe_reader_SYNC0;
            }
            if (syncinput != 0xFF) {
                goto mpipe_reader_SYNC0;
            }
 
            mpipe_reader_SYNC1:
            syncinput = 0;
            new_bytes = (int)read(fds[i].fd, &syncinput, 1);
            if (new_bytes < 1) {
                usleep(10 * 1000);
                goto mpipe_reader_SYNC1;
            }
            if (syncinput == 0xFF) {
                goto mpipe_reader_SYNC1;
            }
            if (syncinput != 0x55) {
                goto mpipe_reader_SYNC0;
            }
            TTY_PRINTF("Sync FF55 Received\n");
 
            // At this point, FF55 was detected.  We get the next 6 bytes of the
            // header, which is the rest of the header.
            new_bytes       = 0;
            payload_left    = 6;
            rbuf_cursor     = rbuf;
            while (payload_left > 0) {
                new_bytes = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                if (new_bytes < 1) {
                    errcode = 4;        // timeout
                    goto mpipe_reader_ERR;
                }
                TTY_PRINTF("header new_bytes = %d\n", new_bytes);
                HEX_DUMP(rbuf_cursor, new_bytes, "read(): ");
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
            }
 
            payload_length  = rbuf[2] * 256;
            payload_length += rbuf[3];
            header_length   = 6 + 0;
 
            if ((payload_length == 0) || (payload_length > (1024-header_length))) {
                errcode = 2;
                goto mpipe_reader_ERR;
            }
 
            // Receive the remaining payload bytes
            payload_left    = payload_length;
            rbuf_cursor     = &rbuf[6];
            while (payload_left > 0) {
                new_bytes = (int)read(fds[i].fd, rbuf_cursor, payload_left);
                if (new_bytes < 1) {
                    errcode = 4;        // timeout
                    goto mpipe_reader_ERR;
                }
                TTY_PRINTF("payload new_bytes = %d\n", new_bytes);
                HEX_DUMP(rbuf_cursor, new_bytes, "read(): ");
                rbuf_cursor    += new_bytes;
                payload_left   -= new_bytes;
            }
*/

