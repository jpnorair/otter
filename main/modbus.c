/* Copyright 2017, JP Norair
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

// Configuration Include
#include "otter_cfg.h"
#if (OTTER_FEATURE(MODBUS) == ENABLED)

// Application Includes
#include "mpipe.h"
#include "modbus.h"
#include "ppipelist.h"

// SMUT Library that has modbus protocol processing
// SMUT is external to otter.
#include <smut.h>

// OT Filesystem modular library
#include <otfs.h>

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


/// DEBUG printing for tty read
#ifdef TTY_DEBUG
#   define TTY_PRINTF(...)  fprintf(stderr, __VA_ARGS__)
#else
#   define TTY_PRINTF(...)  do { } while(0)
#endif







/** Modbus Threads <BR>
  * ========================================================================<BR>
  * <LI> modbus_reader() : manages TTY RX, pushes to rlist.  Depends on no other
  *          thread. </LI>
  * <LI> modbus_writer() : manages TTY TX, gets packets from tlist.  Depends on
  *          dterm_parser() to prepare packet and also waits for modbus_reader()
  *          to be idle. </LI>
  * <LI> modbus_parser() : gets packets from rlist, parses the internal 
  *          protocols, writes output to terminal screen, and manages both rlist
  *          and tlist.  Depends on modbus_reader(), modbus_writer(), and also
  *          dterm_parser(). </LI>
  */

void* modbus_reader(void* args) {
/// Thread that:
/// <LI> Listens to modbus TTY via read(). </LI>
/// <LI> Assembles the packet from TTY data. </LI>
/// <LI> Adds packet into mpipe.rlist, sends cond-sig to mpipe_parser. </LI>

    struct pollfd fds[1];
    int pollcode;
    
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int frame_length;
    int errcode;
    
    // Local copy of MPipe Ctl data: it is used in multiple threads without
    // mutexes (it is read-only anyway)
    mpipe_ctl_t mpctl               = *((mpipe_arg_t*)args)->mpctl;
    
    // The Packet lists are used between threads and thus are Mutexed references
    pktlist_t* rlist                = ((mpipe_arg_t*)args)->rlist;
    pthread_mutex_t* rlist_mutex    = ((mpipe_arg_t*)args)->rlist_mutex;
    pthread_cond_t* pktrx_cond      = ((mpipe_arg_t*)args)->pktrx_cond;

    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    /// Setup for usage of the poll function to flush buffer on read timeouts.
    fds[0].fd       = mpctl.tty_fd;
    fds[0].events   = (POLLIN | POLLNVAL | POLLHUP);
    
    /// Beginning of read loop
    modbus_reader_START:
    mpipe_flush(&mpctl, 0, TCIFLUSH);
    errcode = 0;
    
    /// Otter Modbus Spec 
    /// SOF delay time              1750us
    /// EOF delay time              1750us
    /// Intraframe delay limit      1ms
    
    // Receive the first byte
    // If returns an error, exit
    pollcode = poll(fds, 1, 0);
    if (pollcode <= 0) {
        errcode = 4;
        goto modbus_reader_ERR;
    }
    
    // Receive subsequent bytes
    // abort when there's a long-enough break in reception
    while (1) {
        read(mpctl.tty_fd, rbuf_cursor++, 1);
        
        pollcode = poll(fds, 1, 1);
        
        if (pollcode == 0) {
            // delay limit detected: frame is over
            errcode = 3;
            break;
        }
        else if (pollcode < 0) {
            // some type of error, exit
            errcode = 3;
            goto modbus_reader_ERR;
        }
    }
    
    /// In Modbus, frame length is determined implicitly based on the number
    /// of bytes received prior to a idle-time violation.
    frame_length  = rbuf_cursor - rbuf;
    
    /// Now do some checks to prevent malformed packets.
    if (((unsigned int)frame_length < 4) \ 
    ||  ((unsigned int)frame_length > 256)) {
        errcode = 2;
        goto modbus_reader_ERR;
    }

    /// Copy the packet to the rlist and signal mpipe_parser()
    pthread_mutex_lock(rlist_mutex);
    pktlist_add(rlist, false, rbuf, (size_t)frame_length);
    pthread_mutex_unlock(rlist_mutex);
    
    /// Error Handler: wait a few milliseconds, then handle the error.
    /// @todo supply estimated bytes remaining into mpipe_flush()
    modbus_reader_ERR:
    switch (errcode) {
        case 0: TTY_PRINTF(stderr, "Sending packet rx signal\n");
                pthread_cond_signal(pktrx_cond);
                goto modbus_reader_START;
        
        case 2: TTY_PRINTF(stderr, "Modbus Packet Payload Length is out of bounds.\n");
                goto modbus_reader_START;
                
        case 3: TTY_PRINTF(stderr, "Modbus Packet RX timed-out\n");
                goto modbus_reader_START;
                
        case 4: fprintf(stderr, "Connection dropped, quitting now\n");
                break;
                
       default: fprintf(stderr, "Unknown error, quitting now\n");
                break;
    }
    
    /// This occurs on uncorrected errors, such as case 4 from above, or other 
    /// unknown errors.
    raise(SIGINT);
    return NULL;
}




void* modbus_writer(void* args) {
/// Thread that:
/// <LI> Listens for cond-signal from dterm_prompter() indicating that data has
///          been added to mpipe.tlist, via a cond-signal. </LI>
/// <LI> Sends the packet over the TTY. </LI>
///
    mpipe_ctl_t mpctl                   = *((mpipe_arg_t*)args)->mpctl;
    pktlist_t* tlist                    = ((mpipe_arg_t*)args)->tlist;
    pthread_cond_t* tlist_cond          = ((mpipe_arg_t*)args)->tlist_cond;
    pthread_mutex_t* tlist_cond_mutex   = ((mpipe_arg_t*)args)->tlist_cond_mutex;
    pthread_mutex_t* tlist_mutex        = ((mpipe_arg_t*)args)->tlist_mutex;
    
    while (1) {
        
        ///@note documentation says to have pthread_mutex_lock() ahead of
        /// pthread_cond_wait() for conds, but it doesn't work with this 
        /// configuration.
        //pthread_mutex_lock(tlist_cond_mutex);
        pthread_cond_wait(tlist_cond, tlist_cond_mutex);
        
        pthread_mutex_lock(tlist_mutex);
        
        while (tlist->cursor != NULL) {
            pkt_t* txpkt;
            
            /// Modbus 1.75ms idle SOF
            usleep(1750);
            
            txpkt = tlist->cursor;
            
            // This is a never-before transmitted packet (not a re-transmit)
            // Move to the next in the list.
            if (tlist->cursor == tlist->marker) {
                pkt_t* next_pkt = tlist->marker->next;
                tlist->cursor   = next_pkt;
                tlist->marker   = next_pkt;
            }
            // This is a packet that has just been re-transmitted.  
            // Move to the marker, which is where the new packets start.
            else {
                tlist->cursor   = tlist->marker;
            }
            
            {   int bytes_left;
                int bytes_sent;
                uint8_t* cursor;
                
                time(&txpkt->tstamp);
                cursor      = txpkt->buffer;
                bytes_left  = (int)txpkt->size;
                
#               ifdef TTY_DEBUG
                fprintf(stderr, "Writing %d bytes to tty\n", bytes_left);
                for (int i=0; i<bytes_left; i++) {
                    fprintf(stderr, "%02X ", cursor[i]);
                }
                fprintf(stderr, "\n");
#               endif

                while (bytes_left > 0) {
                    bytes_sent  = (int)write(mpctl.tty_fd, cursor, bytes_left);
                    cursor     += bytes_sent;
                    bytes_left -= bytes_sent;
                }
            }
            
            /// Modbus 1.75ms idle EOF
            usleep(1750);
            
            ///@todo block until RX comes, or some timeout occurs
            
        }
        
        pthread_mutex_unlock(tlist_mutex);
    }
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: modbus_writer() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}



void* modbus_parser(void* args) {
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

    pktlist_t* tlist                = ((mpipe_arg_t*)args)->tlist;
    pktlist_t* rlist                = ((mpipe_arg_t*)args)->rlist;
    mpipe_printer_t _PUTS           = ((mpipe_arg_t*)args)->puts_fn;
    pthread_mutex_t* dtwrite_mutex  = ((mpipe_arg_t*)args)->dtwrite_mutex;
    pthread_mutex_t* rlist_mutex    = ((mpipe_arg_t*)args)->rlist_mutex;
    pthread_mutex_t* tlist_mutex    = ((mpipe_arg_t*)args)->tlist_mutex;
    pthread_cond_t* pktrx_cond      = ((mpipe_arg_t*)args)->pktrx_cond;
    pthread_mutex_t* pktrx_mutex    = ((mpipe_arg_t*)args)->pktrx_mutex;
    cJSON* msgcall                  = ((mpipe_arg_t*)args)->msgcall;
    

    while (1) {
        int pkt_condition;  // tracks some error conditions
    
        //pthread_mutex_lock(pktrx_mutex);
        pthread_cond_wait(pktrx_cond, pktrx_mutex);
        pthread_mutex_lock(dtwrite_mutex);
        pthread_mutex_lock(rlist_mutex);
        pthread_mutex_lock(tlist_mutex);
        
        // This looks like an infinite loop, but is not.  The pkt_condition
        // variable will break the loop if the rlist has no new packets.
        // Otherwise it will parse all new packets, one at a time, until there
        // are none remaining.

        // ===================LOOP CONDITION LOGIC==========================
        
        // pktlist_getnew will validate the packet with CRC:
        // - It returns 0 if all is well
        // - It returns -1 if the list is empty
        // - It returns a positive error code if there is some packet error
        // - rlist->cursor points to the working packet
        pkt_condition = pktlist_getnew(rlist);
        if (pkt_condition < 0) {
            goto mpipe_parser_END;
        }
        // =================================================================
            
        // If packet has an error of some kind -- delete it and move-on.
        // Else, print-out the packet.  This can get rich depending on the
        // internal protocol, and it can result in responses being queued.
        if (pkt_condition > 0) {
            ///@todo some sort of error code
            fprintf(stderr, "A malformed packet was sent for parsing\n");
            pktlist_del(rlist, rlist->cursor);
        }
        else {
            uint16_t    output_bytes;
            int         proc_result;
            bool        clear_rpkt      = true;
            bool        rpkt_is_resp;

            /// For a Modbus master (like this), all received packets are 
            /// responses.  In some type of peer-peer modbus system, this would
            /// need to be intelligently managed.
            rpkt_is_resp = true;
            
            /// If Verbose, Print received header in real language
            /// If not Verbose, just print the encoded packet status
            if (cliopt_isverbose()) { 
                sprintf(putsbuf, "\nRX'ed %zu bytes at %s, %s CRC\n",
                            rlist->cursor->size,
                            fmt_time(&rlist->cursor->tstamp),
                            fmt_crc(rlist->cursor->crcqual)
                        );
            }
            else {
                char crc_symbol = (rlist->cursor->crcqual == 0) ? 'v' : 'x';
                sprintf(putsbuf, "[%c][%03d] ", crc_symbol, rlist->cursor->sequence);
            }
            _PUTS(putsbuf);
            
            /// If CRC is bad, dump hex of buffer-size and discard packet now.
            if (rlist->cursor->crcqual != 0) {
                fmt_printhex(_PUTS, &rlist->cursor->buffer[0], rlist->cursor->size, 16);
                pktlist_del(rlist, rlist->cursor);
                goto mpipe_parser_END;
            }
            
            /// CRC is good, so send packet to Modbus processor.
            /// CRC bytes (2) are stripped
            proc_result = smut_resp_proc(putsbuf, rlist->cursor->buffer, &output_bytes, rlist->cursor->size-2);
            if ((proc_result == 0) && (output_bytes == 0)) {
                fmt_fprintalp(_PUTS, msgcall, putsbuf, payload_bytes);
            }

            // clear_rpkt will always be true.  It means that the received 
            // packet should be cleared from the packet list.
            if (clear_rpkt) {
                pkt_t*  scratch = rlist->cursor;
                pktlist_del(rlist, scratch);
            }
            
            // Clear the tpkt if it exists
            if (tpkt != NULL) {
                pktlist_del(tlist, tpkt);
            }
        } 
        
        mpipe_parser_END:
        pthread_mutex_unlock(tlist_mutex); 
        pthread_mutex_unlock(rlist_mutex);
        pthread_mutex_unlock(dtwrite_mutex); 
        
        ///@todo Can check for major error in pkt_condition
        ///      Major errors are integers less than -1
        
    } // END OF WHILE()
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_parser() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}




#endif  // defined(MODBUS_ENABLED)
