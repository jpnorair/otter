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
#include "mpipe.h"
#include "ppipelist.h"

// Local Libraries/Includes
#include "bintex.h"
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




/// Internal Subroutine Prototypes
void mpipe_flush(mpipe_ctl_t* mpctl, size_t est_rembytes, int queue_selector);
int _get_baudrate(int native_baud);








/** MPipe Control Functions
  * ========================================================================<BR>
  * Typically called by main() to setup the TTY file used by MPipe.
  */
int mpipe_open( mpipe_ctl_t* mpctl,  
                const char *dev, int baud, 
                int data_bits, char parity, int stop_bits, 
                int flowctrl, int dtr, int rts    ) {

    struct termios tio;
    int c_par, i_par;
    
    // See if the dev file input is resembling a serial line filename
    // This routine could be made more thorough
    if (strncmp(dev, "/dev/", 5) < 0) {
        fprintf( stderr, "File %s is not a suitable device file\n", dev );
        return -1;
    }
    
    // first open with O_NDELAY
    if( (mpctl->tty_fd = open( dev, O_RDWR | O_NDELAY | O_EXCL )) < 0 ) {
        fprintf( stderr, "Cannot open device %s\n", dev );
        return -1;
    }
    
    // then reset O_NDELAY
    if( fcntl( mpctl->tty_fd, F_SETFL, O_RDWR ) )  {
        fprintf( stderr, "Cannot fcntl device %s\n", dev );
        return -1;
    }
    
    // clear the datastruct just in case
    bzero(&tio, sizeof(struct termios));
    
    // Set baudrate from int to encoded rate used by termios
    mpctl->baudrate = baud;
    baud            = _get_baudrate(baud);
    if (baud < 0) {
        fprintf(stderr, "baudrate %d is not permitted.  Default baudrate is 115200\n", mpctl->baudrate);
        return -1;
    }
    
    // Make sure byte encoding is compatible with MPipe spec
    if ((data_bits != 8) || (parity != 'N') || (stop_bits != 1)) {
        fprintf(stderr, "In current MPipe implementation, byte encoding MUST be 8N1.  You specifed: %d%c%d\n", 
                    data_bits, parity, stop_bits);
        return -1;
    }
    
    // Make sure all flow control is compatible with MPipe spec (i.e. disabled)
    if ((flowctrl != 0) || (dtr != 0) || (rts != 0)) {
        fprintf(stderr, "In current MPipe implementation there is no flow control.  You specifed:\n"); 
        if (flowctrl != 0)  fprintf(stderr, " - Flowctrl = %d\n", flowctrl);
        if (dtr != 0)       fprintf(stderr, " - DTR enabled\n");
        if (rts != 0)       fprintf(stderr, " - RTS/CTS enabled\n");
        return -1;
    }
    
    // For now, we expect MPipe to use 8N1, no DTR, no RTS/CTS
    data_bits       = CS8;
    i_par           = IGNPAR;
    c_par           = 0;
    stop_bits       = 0;
    flowctrl        = 0;
    tio.c_cflag     = data_bits | stop_bits | c_par | CREAD | CLOCAL | flowctrl;
    tio.c_iflag     = IGNBRK | i_par;
    tio.c_oflag     = CR0 | TAB0 | BS0 | VT0 | FF0;
    tio.c_lflag     = 0;
    tio.c_cc[VMIN]  = 1;        // smallest read is one character... could be changed here
    tio.c_cc[VTIME] = 0;        // no timeout
    
    tcflush( mpctl->tty_fd, TCIFLUSH );
    cfsetospeed( &tio, baud );
    cfsetispeed( &tio, baud );
    
    // Using TCSANOW will do [something]
    if( tcsetattr( mpctl->tty_fd, TCSANOW, &tio ) != 0 )  {
        fprintf( stderr, "Can't set mode of serial line %s\n", ttyname( mpctl->tty_fd ) );
        return -1;
    }
    
    // RTS/CTS are not available at this moment (may never be)
    //if( flowctrl != FLOW_HW )  {
    //    if (rts)    tiocmbis(mpctl->tty_fd,TIOCM_RTS);
    //    else        tiocmbic(mpctl->tty_fd,TIOCM_RTS);
    //}
    
    // DTR is not available at this moment (may never be)
    //if (dtr)    tiocmbis(mpctl->tty_fd,TIOCM_DTR);
    //else        tiocmbic(mpctl->tty_fd,TIOCM_DTR);
    
    return mpctl->tty_fd;
}


int mpipe_close(mpipe_ctl_t* mpctl) {
    return close(mpctl->tty_fd);
}


void mpipe_freelists(pktlist_t* rlist, pktlist_t* tlist) {
    pkt_t* pkt;
    
    // Free the packet buffer, then free the packet itself, then move to the
    // next packet in the list until there are no more.
    pkt = rlist->front;
    while (pkt != NULL) {
        pkt_t* next_pkt = pkt->next;
        if (pkt->buffer != NULL) {
            //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
            free(pkt->buffer);
            //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        }
        //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        free(pkt);
        //fprintf(stderr, "%s %d\n", __FUNCTION__, __LINE__);
        pkt = next_pkt;
    }
    
    ///@todo free tlist
    
}




void mpipe_flush(mpipe_ctl_t* mpctl, size_t est_rembytes, int queue_selector) {
    float       a;
    useconds_t  micros;
    
    a       = (float)(est_rembytes * 10);   // estimated bits remaining in packet
    a      /= (float)mpctl->baudrate;       // estimated seconds remaining in packet
    micros  = (useconds_t)(a * 1000000.f);  // est microseconds
    
    usleep(micros);
    tcflush(mpctl->tty_fd, queue_selector);
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
    struct pollfd fds[1];
    int pollcode;
    
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int header_length;
    int payload_length;
    int payload_left;
    int errcode;
    int new_bytes;
    uint8_t syncinput;
    
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
    mpipe_reader_START:
    mpipe_flush(&mpctl, 0, TCIFLUSH);
    errcode = 0;
    
    /// Wait until an FF comes
    mpipe_reader_SYNC0:
    pollcode = poll(fds, 1, 0);
    if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        errcode = 4;
        goto mpipe_reader_ERR;
    }
    
    new_bytes = (int)read(mpctl.tty_fd, &syncinput, 1);
    if (new_bytes < 1) {
        errcode = 1;
        goto mpipe_reader_ERR;
    }
    if (syncinput != 0xFF) {
        goto mpipe_reader_SYNC0;
    }
    
    /// Now wait for a 55, ignoring FFs
    mpipe_reader_SYNC1:
    pollcode = poll(fds, 1, 50);
    if (pollcode <= 0) {
        errcode = 3;
        goto mpipe_reader_ERR;
    }
    else if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
        errcode = 4;
        goto mpipe_reader_ERR;
    }
    
    new_bytes = (int)read(mpctl.tty_fd, &syncinput, 1);
    if (new_bytes < 1) {
        errcode = 1;
        goto mpipe_reader_ERR;
    }
    if (syncinput == 0xFF) {
        goto mpipe_reader_SYNC1;
    }
    if (syncinput != 0x55) {
        goto mpipe_reader_SYNC0;
    }
    
    //fprintf(stderr, "Sync Found\n");
    
    /// At this point, FF55 was detected.  We get the next 6 bytes of the 
    /// header, which is the rest of the header.  
    /// @todo Add a timeout mechanism here.
    /// @todo Make header length dynamic based on control field (last byte).
    ///           However, control field is not yet defined.
    
    ///@todo Set kill timer for receiving header bytes
    
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
    
        new_bytes       = (int)read(mpctl.tty_fd, rbuf_cursor, payload_left);
        //fprintf(stderr, "new_bytes = %d\n", new_bytes);
        rbuf_cursor    += new_bytes;
        payload_left   -= new_bytes;
        
    } while (payload_left > 0);
    
    if (payload_left != 0) {
        goto mpipe_reader_START;
    }
    
    /// Bytes 0:1 are the CRC16 checksum.  The controlling app can decide what
    /// to do about those.  They are in the buffer.
    
    /// Bytes 2:3 are the Length of the Payload, in big endian.  It can be up
    /// to 65535 bytes, but CRC16 won't be great help for more than 250 bytes.
    payload_length  = rbuf[2] * 256;
    payload_length += rbuf[3];
    
    /// Byte 4 is a sequence number, which the controlling app can decide what
    /// to do with.  Byte 5 is RFU, but in the future it can be used to 
    /// expand the header.  That logic would go below (currently trivial).
    header_length = 6 + 0;
    
    /// Now do some checks to prevent malformed packets.
    if (((unsigned int)payload_length == 0) \
    || ((unsigned int)payload_length > (1024-header_length))) {
        errcode = 2;
        goto mpipe_reader_ERR;
    }
    
    /// Receive the remaining payload bytes
    ///@todo Re-set kill timer for receiving payload bytes
    ///@note Commented-out parts are buffer printouts for data alignment 
    ///      validation.
    ///      
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
    
        new_bytes       = (int)read(mpctl.tty_fd, rbuf_cursor, payload_left);
        //fprintf(stderr, "new_bytes = %d\n", new_bytes);
        
//        fprintf(stderr, "read(): ");
//        for (int i=0; i<new_bytes; i++) {
//            fprintf(stderr, "%02X ", rbuf_cursor[i]);
//        }
//        fprintf(stderr, "\n");
        
        rbuf_cursor    += new_bytes;
        payload_left   -= new_bytes;
    };

//    fprintf(stderr, "pkt   : ");
//    for (int i=0; i<payload_length; i++) {
//        fprintf(stderr, "%02X ", rbuf[6+i]);
//    }
//    fprintf(stderr, "\n");

    /// Copy the packet to the rlist and signal mpipe_parser()
    pthread_mutex_lock(rlist_mutex);
    pktlist_add(rlist, false, rbuf, (size_t)(header_length + payload_length));
    pthread_mutex_unlock(rlist_mutex);
    
    /// Error Handler: wait a few milliseconds, then handle the error.
    /// @todo supply estimated bytes remaining into mpipe_flush()
    mpipe_reader_ERR:
    switch (errcode) {
        case 0: //fprintf(stderr, "sending packet rx signal\n");
                pthread_cond_signal(pktrx_cond);
                goto mpipe_reader_START;
        
        case 1: // send error "MPipe Packet Sync could not be retrieved."
                goto mpipe_reader_START;
        
        case 2: // send error "Mpipe Packet Payload Length is out of bounds."
                goto mpipe_reader_START;
                
        case 3: // send error "Mpipe Packet RX timed-out
                goto mpipe_reader_START;
                
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




void* mpipe_writer(void* args) {
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
        
        //pthread_mutex_lock(tlist_cond_mutex);
        pthread_cond_wait(tlist_cond, tlist_cond_mutex);
        pthread_mutex_lock(tlist_mutex);
        
        while (tlist->cursor != NULL) {
            pkt_t* txpkt;
            
            ///@todo wait also for RX to be off
            
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
                
                while (bytes_left > 0) {
                    bytes_sent  = (int)write(mpctl.tty_fd, cursor, bytes_left);
                    cursor     += bytes_sent;
                    bytes_left -= bytes_sent;
                }
            }
            
            // We put a an interval between transmissions in order to 
            // facilitate certain blocking implementations of the target MPipe.
            /// @todo make configurable instead of 10ms
            usleep(10000);
        }
        
        pthread_mutex_unlock(tlist_mutex);
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
        //while (1) {
            // ===================LOOP CONDITION LOGIC==========================
            
            // mpipe_getnew will validate and decrypt the packet:
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
                pkt_t*      tpkt;
                uint8_t*    payload_front;
                size_t      payload_bytes;
                bool        clear_rpkt      = true;
                bool        rpkt_is_resp    = false;
                bool        rpkt_is_valid   = false;
                
                // Response Packets should match to a sequence number of the last Request.
                // If there is no match, then there is no response processing, we just 
                // print the damn packet.
                tpkt = tlist->front;
                while (tpkt != NULL) {
                    if (tpkt->sequence == rlist->cursor->sequence) {
                        // matching transmitted packet: it IS a response
                        rpkt_is_resp = true;
                        break;
                    }
                    tpkt = tpkt->next;
                }
                
                // If Verbose, Print received header in real language
                // If not Verbose, just print the encoded packet status
                ///@todo integrate CLI options
                if (true) { //(cli.opt.verbose_on) {
                    sprintf(putsbuf, "\nRX'ed %zu bytes at %s, %s CRC: %s\n",
                                rlist->cursor->size,
                                fmt_time(&rlist->cursor->tstamp),
                                fmt_crc(rlist->cursor->crcqual),
                                fmt_hexdump_header(rlist->cursor->buffer)
                            );
                }
                else {
                    char crc_symbol = (rlist->cursor->crcqual == 0) ? 'v' : 'x';
                    sprintf(putsbuf, "[%c][%03d] ", crc_symbol, rlist->cursor->sequence);
                }
                _PUTS(putsbuf);
                
                /// If CRC is bad, dump hex of buffer-size and discard the 
                /// packet now.
                ///@note in present versions there is some unreliability with
                ///      CRC: possibly data alignment or something, or might be
                ///      a problem on the test sender.
                if (rlist->cursor->crcqual != 0) {
                    fmt_printhex(_PUTS, &rlist->cursor->buffer[6], rlist->cursor->size-6, 16);
                    pktlist_del(rlist, rlist->cursor);
                    goto mpipe_parser_END;
                }
                
                // Here is where decryption would go
                if (rlist->cursor->buffer[5] & (3<<5)) {
                    ///@todo Deal with encryption here.  When implemented, there
                    /// should be an encryption header at this offset (6), 
                    /// followed by the payload, and then the real data payload.
                    /// The real data payload is followed by a 4 byte Message
                    /// Authentication Check (Crypto-MAC) value.  AES128 EAX
                    /// is the cryptography and cipher used.
                    payload_front = &rlist->cursor->buffer[6];  
                }
                else {
                    payload_front = &rlist->cursor->buffer[6];
                }
                
                // Inspect header to see if M2DEF
                if ((rlist->cursor->buffer[5] & (1<<7)) == 0) {
                    rpkt_is_valid = true;
                    //parse some shit
                    //clear_rpkt = false when there is non-atomic input
                }
                
                // Get Payload Bytes, found in buffer[2:3]
                // Then print-out the payload.
                // If it is a M2DEF payload, the print-out can be formatted in different ways
                payload_bytes   = rlist->cursor->buffer[2] * 256;
                payload_bytes  += rlist->cursor->buffer[3];

                // Send an error if payload bytes is too big
                if (payload_bytes > rlist->cursor->size) {
                    sprintf(putsbuf, "... Reported Payload Length (%zu) is larger than buffer (%zu).\n" \
                                     "... Possible transmission error.\n",
                                    payload_bytes, rlist->cursor->size
                            );
                    _PUTS(putsbuf);
                }
                
                ///@todo implement m2def library
                else if (rpkt_is_valid)  {
                    //m2def_sprintf(putsbuf, &rlist->cursor->buffer[6], 2048, "");
                    //_PUTS(putsbuf)
                    fmt_fprintalp(_PUTS, msgcall, payload_front, payload_bytes);
                }
                
                /// Dump hex if not using ALP
                else {
                    fmt_printhex(_PUTS, payload_front, payload_bytes, 16);
                }

                // Clear the rpkt if required, and move the cursor to the next
                if (clear_rpkt) {
                    pkt_t*  scratch = rlist->cursor;
                    //rlist->cursor   = rlist->cursor->next;
                    pktlist_del(rlist, scratch);
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
                    pktlist_del(tlist, tpkt);
                }
            } 
            
            
        
        //} // END OF WHILE()
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








int _get_baudrate(int native_baud) {
    int bd;
    
    switch( native_baud )  {
        case     50 : bd =     B50; break;
        case     75 : bd =     B75; break;
        case    110 : bd =    B110; break;
        case    134 : bd =    B134; break;
        case    150 : bd =    B150; break;
        case    200 : bd =    B200; break;
        case    300 : bd =    B300; break;
        case    600 : bd =    B600; break;
        case   1200 : bd =   B1200; break;
        case   1800 : bd =   B1800; break;
        case   2400 : bd =   B2400; break;
        case   4800 : bd =   B4800; break;
        case   9600 : bd =   B9600; break;
        case  19200 : bd =  B19200; break;
        case  38400 : bd =  B38400; break;
        case  57600 : bd =  B57600; break;
        case 115200 : bd = B115200; break;
#ifdef B128000
        case 128000 : bd = B128000; break;
#endif
        case 230400 : bd = B230400; break;
#ifdef B256000
        case 256000 : bd = B256000; break;
#endif
        default: bd = -1; break;            /* invalid baudrate */
    }
    return bd;
}



