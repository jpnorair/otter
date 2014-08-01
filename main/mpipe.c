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
#include "crc_calc_block.h"
#include "mpipe.h"

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





/// Internal Subroutine Prototypes
void mpipe_flush(mpipe_ctl_t* mpctl, size_t est_rembytes, int queue_selector);
int _get_baudrate(int native_baud);


// Formatting functions that I suspect are internal to mpipe, but which could
// certainly be stuck into their own module.
void _fprinthex(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes, size_t cols);
void _fprintalp(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes);

void _hexdump_raw(char* dst, uint8_t* src, size_t src_bytes);
char* _hexdump_header(uint8_t* data);
char* _format_crc(unsigned int crcqual);
char* _format_time(time_t* tstamp);
void _print_usage(const char* program_name);







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
        free(pkt->buffer);
        free(pkt);
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
    uint8_t sync;
    uint8_t syncinput;
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int header_length;
    int payload_length;
    int errcode;
    int new_bytes;
    
    // Local copy of MPipe Ctl data: it is used in multiple threads without
    // mutexes (it is read-only anyway)
    mpipe_ctl_t mpctl               = *((mpipe_arg_t*)args)->mpctl;
    
    // The Packet lists are used between threads and thus are Mutexed references
    pktlist_t* rlist                = ((mpipe_arg_t*)args)->rlist;
    pthread_mutex_t* rlist_mutex    = ((mpipe_arg_t*)args)->rlist_mutex;
    pthread_cond_t* pktrx_cond      = ((mpipe_arg_t*)args)->pktrx_cond;

    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    mpipe_reader_START:
    errcode     = 0;
    sync        = 0xff;
    
    /// Look for the first two words of SYNC: FF55.
    mpipe_reader_SYNC:
    new_bytes = (int)read(mpctl.tty_fd, &syncinput, 1);
    if (new_bytes < 1) {
        errcode = 1;
        goto mpipe_reader_ERR;
    }
    if (syncinput != sync) {
        goto mpipe_reader_START;
    }
    if (sync == 0xff) {
        sync = 0x55;
        goto mpipe_reader_SYNC;
    }
    
    /// At this point, FF55 was detected.  We get the next 6 bytes of the 
    /// header, which is the rest of the header.  
    /// @todo Add a timeout mechanism here.
    /// @todo Make header length dynamic based on control field (last byte).
    ///           However, control field is not yet defined.
    new_bytes   = 0;
    rbuf_cursor = rbuf;
    do {
        new_bytes   += (int)read(mpctl.tty_fd, rbuf_cursor, 6-new_bytes);
        rbuf_cursor += new_bytes;
    } while (new_bytes < 6);
    
    if (new_bytes != 6) {
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
    
    /// Now do some checks to prevent malformed packets and to receive the 
    /// payload, then receive the payload.
    if (((unsigned int)payload_length == 0) \
    || ((unsigned int)payload_length > (1024-header_length))) {
        errcode = 2;
        goto mpipe_reader_ERR;
    }
    do { 
        new_bytes       = (int)read(mpctl.tty_fd, rbuf_cursor, payload_length);
        rbuf_cursor    += new_bytes;
        payload_length -= new_bytes;
    } while (payload_length > 0);

    /// Copy the packet to the rlist and signal mpipe_parser()
    pthread_mutex_lock(rlist_mutex);
    pktlist_add(rlist, rbuf, (size_t)(header_length + payload_length));
    pthread_mutex_unlock(rlist_mutex);
    
    /// Error Handler: wait a few milliseconds, then handle the error.
    /// @todo supply estimated bytes remaining into mpipe_flush()
    mpipe_reader_ERR:
    switch (errcode) {
        case 0: pthread_cond_signal(pktrx_cond);
                goto mpipe_reader_START;
        
        case 1: // send error "MPipe Packet Sync could not be retrieved."
                mpipe_flush(&mpctl, 0, TCIFLUSH);
                goto mpipe_reader_START;
        
        case 2: // send error "Mpipe Packet Payload Length is out of bounds."
                mpipe_flush(&mpctl, 0, TCIFLUSH);
                goto mpipe_reader_START;
    }
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_reader() thread broke loop.\n");
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
        pthread_mutex_unlock(tlist_cond_mutex);
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
    

    while (1) {
        pthread_cond_wait(pktrx_cond, pktrx_mutex);
        pthread_mutex_lock(dtwrite_mutex);
        pthread_mutex_lock(rlist_mutex);
        pthread_mutex_lock(tlist_mutex);
        
        // This looks like an infinite loop, but is not.  The pkt_condition
        // variable will break the loop if the rlist has no new packets.
        // Otherwise it will parse all new packets, one at a time, until there
        // are none remaining.
        while (1) {
            
            // ===================LOOP CONDITION LOGIC==========================
            int pkt_condition;
            
            // mpipe_getnew will validate and decrypt the packet:
            // - It returns 0 if all is well
            // - It returns -1 if the list is empty
            // - It returns a positive error code if there is some packet error
            // - rlist->cursor points to the working packet
            pkt_condition = pktlist_getnew(rlist);
            if (pkt_condition < 0) {
                break;
            }
            // =================================================================
            
            // If packet has an error of some kind -- delete it and move-on.
            // Else, print-out the packet.  This can get rich depending on the
            // internal protocol, and it can result in responses being queued.
            if (pkt_condition > 0) {
                ///@todo some sort of error code
                pktlist_del(rlist->cursor);
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
                                _format_time(&rlist->cursor->tstamp),
                                _format_crc(rlist->cursor->crcqual),
                                _hexdump_header(rlist->cursor->buffer)
                            );
                }
                else {
                    char crc_symbol = (rlist->cursor->crcqual == 0) ? 'v' : 'x';
                    sprintf(putsbuf, "[%c][%03d] ", crc_symbol, rlist->cursor->sequence);
                }
                _PUTS(putsbuf);
                
                
                // Here is where decryption would go
                if (rlist->cursor->buffer[5] & (3<<5)) {
                    payload_front = &rlist->cursor->buffer[6];  ///@todo deal with encryption
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
                ///@todo implement m2def library
                //if (rpkt_is_valid)  {
                    //m2def_sprintf(putsbuf, &rlist->cursor->buffer[6], 2048, "");
                    //_PUTS(putsbuf);
                //}
                //else {
                    _fprinthex(_PUTS, payload_front, payload_bytes, 16);
                //}

                // Clear the rpkt if required, and move the cursor to the next
                if (clear_rpkt) {
                    pkt_t*  scratch = rlist->cursor;
                    rlist->cursor   = rlist->cursor->next;
                    pktlist_del(scratch);
                }
                
                // Clear the tpkt if it is matched with an rpkt
                // Also, clear the oldest tpkt if it's timestamp is of a certain amout.
                ///@todo Change timestamp so that it is not hardcoded
                if (rpkt_is_resp == false) {
                    if (rlist->front->tstamp == 0) {    ///@todo check if old
                        tpkt = tlist->front;
                        goto mpipe_parse_DELTPKT;
                    }
                }
                else {
                mpipe_parse_DELTPKT:
                    if (tpkt == tlist->cursor) {
                        tlist->cursor = tlist->cursor->next;
                    }
                    pktlist_del(tpkt);
                }
            } 
        } // END OF WHILE()
        
        pthread_mutex_unlock(tlist_mutex);
        pthread_mutex_unlock(rlist_mutex);
        pthread_mutex_unlock(dtwrite_mutex);
        pthread_mutex_unlock(pktrx_mutex); 
    } // END OF WHILE()
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: mpipe_parser() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}









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




int pktlist_add(pktlist_t* plist, uint8_t* data, size_t size) {
    pkt_t* newpkt;
    
    if (plist == NULL) {
        return -1;
    }
    
    newpkt = malloc(sizeof(pkt_t));
    if (newpkt == NULL) {
        return -2;
    }
    
    // Setup list connections for the new packet
    // Also allocate the buffer of the new packet
    ///@todo Change the hardcoded +8 to a dynamic detection of the header
    ///      length, which depends on current mode settings in the "cli".
    ///      Dynamic header isn't implemented yet, so no rush.
    newpkt->prev    = plist->last;
    newpkt->next    = NULL;
    newpkt->size    = size+8;
    newpkt->buffer  = malloc(newpkt->size);
    if (newpkt->buffer == NULL) {
        return -3;
    }
    
    // Copy Payload into Packet buffer, leaving room for header
    memcpy(&newpkt->buffer[8], data, newpkt->size);
    newpkt->buffer[0]   = 0xff;
    newpkt->buffer[1]   = 0x55;
    newpkt->buffer[2]   = 0;
    newpkt->buffer[3]   = 0;
    newpkt->buffer[4]   = size >> 8;
    newpkt->buffer[5]   = size & 0xff;
    newpkt->buffer[6]   = 0;
    newpkt->buffer[7]   = 0;            ///@todo Set Control Field here based on Cli.
    
    // List is empty, so start the list
    if (plist->last == NULL) {
        newpkt->sequence    = 0;
        plist->size         = 0;
        plist->front        = newpkt;
        plist->last         = newpkt;
        plist->cursor       = newpkt;
        plist->marker       = newpkt;
    }
    // List is not empty, so simply extend the list
    else {
        newpkt->sequence    = plist->last->sequence + 1;
        plist->last->next   = newpkt;
        plist->last         = plist->last->next;
    }
    
    ///@todo Move Sequence Number entry and CRC entry to somewhere in writer
    ///      thread, so that it can be retransmitted with new sequence
    {   uint16_t crcval;
        newpkt->buffer[6]   = newpkt->sequence;
        crcval              = crc_calc_block(&newpkt->buffer[4], newpkt->size - 4);
        newpkt->buffer[2]   = crcval >> 8;
        newpkt->buffer[3]   = crcval & 0xff;
    }
    
    // Increment the list size to account for new packet
    plist->size++;
    
    return (int)plist->size;
}



int pktlist_del(pkt_t* pkt) {
    pkt_t* prev;
    pkt_t* next;
    
    if (pkt == NULL) {
        return -1;
    }
    
    prev = pkt->prev;
    next = pkt->next;
    
    // Delete this packet
    if (pkt->buffer != NULL) {
        free(pkt->buffer);
    }
    free(pkt);
    
    // Stitch the list back together
    prev->next  = next;
    next->prev  = prev;
    
    return 0;
}



int pktlist_getnew(pktlist_t* plist) {
    uint16_t    crc_val;
    uint16_t    crc_comp;
    //time_t      seconds;

    ///@todo Is it needed to do mpipe_add() here?  I don't think so
    
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













void _fprinthex(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes, size_t cols) {
    const char convert[] = "0123456789ABCDEF";
    size_t i = cols;
    
    while (src_bytes-- != 0) {
        char hexstr[4] = {0, 0, 0, 0};
        
        hexstr[0] = convert[*src >> 4];
        hexstr[1] = convert[*src & 0x0f];
        hexstr[2] = ' ';
        src++;
        puts_fn(hexstr);
        
        if ((i == 0) || (src_bytes == 0)) {
            i = cols;
            puts_fn("\n");
        }
    }
}


void _fprintalp(mpipe_printer_t puts_fn, uint8_t* src, size_t src_bytes) {
    ///@todo Build an output formatter
    _fprinthex(puts_fn, src, src_bytes, 16);
}



void _hexdump_raw(char* dst, uint8_t* src, size_t src_bytes) {
    const char convert[] = "0123456789ABCDEF";
    
    //convert to hex
    while (src_bytes-- != 0) {
        *dst++  = convert[(*src >> 4)];
        *dst++  = convert[(*src & 0x0f)];
        *dst++  = ' ';
        src++;
    }
    dst--;              // clip last "space" character
    *dst = 0;           // add termination character to string
}


char* _hexdump_header(uint8_t* data) {
    static char hexdump_buf[32*3 + 1];
    
    /// @todo header inspection to determine length.
    ///       this entails looking at CONTROL field to see encryption type
    
    _hexdump_raw(hexdump_buf, data, 6);
    
    return hexdump_buf;
}


char* _format_crc(unsigned int crcqual) {
    static char invalid[] = "invalid";
    static char valid[] = "valid";
    return (crcqual) ? invalid : valid;
}


char* _format_time(time_t* tstamp) {
    static char time_buf[24];
    
    // convert to time using time.h library functions
    strftime(time_buf, 24, "%T", localtime(tstamp) );
    
    return time_buf;
}







