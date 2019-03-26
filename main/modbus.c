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
  */

// Configuration Include
#include "otter_cfg.h"
#if (OTTER_FEATURE_MODBUS == ENABLED)

// Application Includes
#include "cliopt.h"
#include "debug.h"
#include "dterm.h"
#include "mpipe.h"
#include "modbus.h"
#include "ppipelist.h"
#include "formatters.h"

// OT Filesystem modular library
#include <otfs.h>

// SMUT Library that has modbus protocol processing
// SMUT is external to otter.
#include <smut.h>

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



static dterm_fd_t* modbus_active_dterm;

static int sub_dtputs(char* str) {
    return dterm_puts(modbus_active_dterm, str);
}




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
/// <LI> Adds packet into mpipe.rlist, sends cond-sig to modbus_parser. </LI>
    otter_app_t* appdata    = args;
    struct pollfd* fds      = NULL;
    mpipe_handle_t mph;
    int num_fds;
    int pollcode;
    int ready_fds;
    
    uint8_t rbuf[1024];
    uint8_t* rbuf_cursor;
    int read_limit;
    int frame_length;
    int list_size;
    int errcode;
    
    if (appdata == NULL) {
        goto modbus_reader_TERM;
    }
    
    // Local copy of MPipe Ctl data: it is used in multiple threads without
    // mutexes (it is read-only anyway)
    mph = appdata->mpipe;
    
    // The Packet lists are used between threads and thus are Mutexed references
//    pktlist_t* rlist                = appdata->rlist;
//    pthread_mutex_t* rlist_mutex    = appdata->rlist_mutex;
//    pthread_cond_t* pktrx_cond      = appdata->pktrx_cond;
//    user_endpoint_t* endpoint       = &appdata->endpoint;

    // blocking should be in initialization... Here just as a reminder
    //fnctl(dt->fd_in, F_SETFL, 0);  
    
    /// Setup for usage of the poll function to flush buffer on read timeouts.
    num_fds = mpipe_pollfd_alloc(mph, &fds, (POLLIN | POLLNVAL | POLLHUP));
    if (num_fds <= 0) {
        fprintf(stderr, "Modbus polling could not be started (error %i): quitting\n", num_fds);
        goto modbus_reader_TERM;
    }
    
    while (1) {
        errcode = 0;
        
        // This will flush all fds, which we definitely don't want to do
        //mpipe_flush(mph, -1, 0, TCIFLUSH);
        
        /// Otter Modbus Spec
        /// SOF delay time              1750us
        /// EOF delay time              1750us
        /// Intraframe delay limit      1ms
        
        // Receive the first byte
        // If returns an error, exit
        ready_fds = poll(fds, num_fds, -1);
        if (ready_fds <= 0) {
            fprintf(stderr, "Polling failure: quitting now\n");
            goto modbus_reader_TERM;
        }
    
        for (int i=0; i<num_fds; i++) {
            // Handle Errors
            ///@todo change 100ms fixed wait on hangup to a configurable amount
            if (fds[i].revents & (POLLNVAL|POLLHUP)) {
                usleep(100 * 1000);
                if (mpipe_reopen(mph, i) == 0) {
                    mpipe_flush(mph, i, 0, TCIFLUSH);
                    continue;
                }
                else {
                    errcode = 5;
                    goto modbus_reader_ERR;
                }
            }
            
            // Verify that POLLIN is high.  This should be implicit, but we check explicitly here
            if ((fds[i].revents & POLLIN) == 0) {
                mpipe_flush(mph, i, 0, TCIFLUSH);
                continue;
            }
            
            // Receive subsequent bytes
            // abort when there's a long-enough break in reception
            rbuf_cursor = rbuf;
            read_limit  = 1024;
            while (read_limit > 0) {
                int new_bytes;
                
                // 1. Read all bytes that have been received, but don't go beyond limit
                new_bytes       = read(fds[i].fd, rbuf_cursor, read_limit);
                rbuf_cursor    += new_bytes;
                read_limit     -= new_bytes;
                
                // 2. Poll function will do a timeout waiting for next byte(s).  In Modbus,
                //    data timeout means end-of-frame.
                pollcode = poll(&fds[i], 1, OTTER_PARAM(MBTIMEOUT));
                if (pollcode == 0) {
                    // delay limit detected: frame is over
                    break;
                }
                else if (pollcode < 0) {
                    // some type of error, exit
                    errcode = 4;
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

            /// Copy the packet to the rlist and signal modbus_parser()
            pthread_mutex_lock(appdata->rlist_mutex);
            list_size = pktlist_add_rx(&appdata->endpoint, mpipe_intf_get(mph, i), appdata->rlist, rbuf, (size_t)frame_length);
            pthread_mutex_unlock(appdata->rlist_mutex);
            
            if (list_size <= 0) {
                errcode = 3;
            }
        
            /// Error Handler: wait a few milliseconds, then handle the error.
            /// @todo supply estimated bytes remaining into modbus_flush()
            modbus_reader_ERR:
            switch (errcode) {
                case 0: TTY_RX_PRINTF("Packet Received Successfully (%d bytes).\n", frame_length);
                        HEX_DUMP(rbuf, frame_length, "Reading %d Bytes on tty\n", frame_length);
                        pthread_cond_signal(appdata->pktrx_cond);
                        break;
                
                case 2: TTY_RX_PRINTF("Modbus Packet Payload Length (%d bytes) is out of bounds.\n", frame_length);
                        break;
                    
                case 3: TTY_RX_PRINTF("Modbus Packet frame has invalid data (bad crypto or CRC).\n");
                        break;
                    
                case 4: TTY_RX_PRINTF("Modbus Packet RX timed-out\n");
                        break;
                    
                case 5: fprintf(stderr, "Dropped %s: quitting now\n", mpipe_file_get(mph, i));
                        goto modbus_reader_TERM;
                    
               default: fprintf(stderr, "Unknown Error during tty polling: quitting now\n");
                        goto modbus_reader_TERM;
            }
        }
    }
    
    /// ---------------------------------------------------------------------
    /// Code below this line occurs during fatal errors
    
    modbus_reader_TERM:
    mpipe_flush(mph, -1, 0, TCIFLUSH);
    
    if (fds != NULL) {
        free(fds);
    }
    
    raise(SIGINT);
    return NULL;
}



///@todo make this mpipe_send_to_intf()
static void sub_write_on_intf(void* intf, uint8_t* data, int data_bytes) {
    int sent_bytes;
    mpipe_fd_t* ifds;
    
    if ((intf != NULL) && (data != NULL)) {
        ifds = mpipe_fds_resolve(intf);
        if (ifds != NULL) {
            HEX_DUMP(data, data_bytes, "Writing %d bytes to %s\n", data_bytes, mpipe_file_resolve(intf));
            
            while (data_bytes > 0) {
                sent_bytes  = (int)write(ifds->out, data, data_bytes);
                data       += sent_bytes;
                data_bytes -= sent_bytes;
            }
        }
    }
}



void* modbus_writer(void* args) {
/// Thread that:
/// <LI> Listens for cond-signal from dterm_prompter() indicating that data has
///          been added to mpipe.tlist, via a cond-signal. </LI>
/// <LI> Sends the packet over the TTY. </LI>
///
    otter_app_t* appdata = args;
    mpipe_handle_t mph;
//    mpipe_handle_t mph                  = ((mpipe_arg_t*)args)->handle;
//    pktlist_t* tlist                    = ((mpipe_arg_t*)args)->tlist;
//    pthread_cond_t* tlist_cond          = ((mpipe_arg_t*)args)->tlist_cond;
//    pthread_mutex_t* tlist_cond_mutex   = ((mpipe_arg_t*)args)->tlist_cond_mutex;
//    pthread_mutex_t* tlist_mutex        = ((mpipe_arg_t*)args)->tlist_mutex;
    
    if (appdata == NULL) {
        goto modbus_writer_TERM;
    }
    
    mph = appdata->mpipe;
    
    while (1) {
        
        ///@note documentation says to have pthread_mutex_lock() ahead of
        /// pthread_cond_wait() for conds, but it doesn't work with this 
        /// configuration.
        //pthread_mutex_lock(tlist_cond_mutex);
        pthread_cond_wait(appdata->tlist_cond, appdata->tlist_cond_mutex);
        
        pthread_mutex_lock(appdata->tlist_mutex);
        
        while (appdata->tlist->cursor != NULL) {
            pkt_t* txpkt;
            
            /// Modbus 1.75ms idle SOF
            usleep(1750);
            
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
            
            time(&txpkt->tstamp);
            
            if (txpkt->intf == NULL) {
                int id_i = (int)mpipe_numintf_get(mph);
                while (id_i >= 0) {
                    id_i--;
                    sub_write_on_intf(mpipe_intf_get(mph, id_i), txpkt->buffer, (int)txpkt->size);
                }
            }
            else {
                sub_write_on_intf(txpkt->intf, txpkt->buffer, (int)txpkt->size);
            }
            
            /// Modbus 1.75ms idle EOF
            usleep(1750);
            
            ///@todo block until RX comes, or some timeout occurs
            
        }
        
        pthread_mutex_unlock(appdata->tlist_mutex);
    }
    
    modbus_writer_TERM:
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: modbus_writer() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}






void* modbus_parser(void* args) {
///@todo wait for modbus_writer() to complete before killing any tlist data.
///      A mutex could work here.
///@todo amputate tlist if it gets too big

/// Thread that:
/// <LI> Waits for cond-sig from modbus_reader() when new packet(s) exist. </LI>
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
        goto modbus_parser_TERM;
    }

    dth = appdata->dterm_parent;
    if (dth->ext != appdata) {
        fprintf(stderr, "Error: dterm handle is not linked to dterm_parent in application data.\n");
        goto modbus_parser_TERM;
    }
    
    while (1) {
        int pkt_condition;  // tracks some error conditions
    
        //pthread_mutex_lock(pktrx_mutex);
        pthread_cond_wait(appdata->pktrx_cond, appdata->pktrx_mutex);
        pthread_mutex_lock(dth->iso_mutex);
        pthread_mutex_lock(appdata->rlist_mutex);
        pthread_mutex_lock(appdata->tlist_mutex);
        
        modbus_active_dterm = &dth->fd;
        
        // This looks like an infinite loop, but is not.  The pkt_condition
        // variable will break the loop if the rlist has no new packets.
        // Otherwise it will parse all new packets, one at a time, until there
        // are none remaining.

        // ===================LOOP CONDITION LOGIC==========================
        
        /// pktlist_getnew will validate the packet with CRC:
        /// - It returns 0 if all is well
        /// - It returns -1 if the list is empty
        /// - It returns a positive error code if there is some packet error
        /// - rlist->cursor points to the working packet
        pkt_condition = pktlist_getnew(appdata->rlist);
        if (pkt_condition < 0) {
            goto modbus_parser_END;
        }
        // =================================================================
            
        /// If packet has an error of some kind -- delete it and move-on.
        /// Else, print-out the packet.  This can get rich depending on the
        /// internal protocol, and it can result in responses being queued.
        if (pkt_condition > 0) {
            ///@todo some sort of error code
            fprintf(stderr, "A malformed packet was sent for parsing\n");
            pktlist_del(appdata->rlist, appdata->rlist->cursor);
        }
        else {
            uint16_t    smut_outbytes;
            uint16_t    smut_msgbytes;
            int         proc_result;
            int         msgtype;
            uint8_t*    msg;
            int         msgbytes;
            bool        rpkt_is_resp;
            pkt_t*      rpkt;

            /// For a Modbus master (like this), all received packets are 
            /// responses.  In some type of peer-peer modbus system, this would
            /// need to be intelligently managed.
            rpkt_is_resp    = true;
            rpkt            = appdata->rlist->cursor;

            /// If CRC is bad, discard packet now, and rxstat an error
            /// CRC is good, so send packet to Modbus processor.
            if (appdata->rlist->cursor->crcqual != 0) {
                ///@todo add rx address of input packet (set to 0)
                dterm_output_rxstat(dth, DFMT_Binary, rpkt->buffer, rpkt->size, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
            }
            else {
                proc_result     = smut_resp_proc(putsbuf, rpkt->buffer, &smut_outbytes, rpkt->size, true);
                msg             = rpkt->buffer;
                smut_msgbytes   = rpkt->size;
                msgtype         = smut_extract_payload((void**)&msg, (void*)msg, &smut_msgbytes, smut_msgbytes, true);
                msgbytes        = smut_msgbytes;
                
                while (msgbytes > 0) {
                    DFMT_Type rxstat_fmt;
                    int subsig;
                    size_t putsbytes = 0;
                    uint8_t* lastmsg = msg;
                
                    if ((proc_result == 0) && (msgtype == 0)) {
                        /// ALP message:
                        /// proc_result now takes the value from the protocol formatter.
                        /// The formatter will give negative values on framing errors
                        /// and also for protocol errors (i.e. NACKs).
                        proc_result = fmt_fprintalp((uint8_t*)putsbuf, &putsbytes, &msg, msgbytes);
                        rxstat_fmt  = DFMT_Native;
                        
                        /// Successful formatted output gets propagated to any
                        /// subscribers of this ALP ID.
                        subsig = (proc_result >= 0) ? SUBSCR_SIG_OK : SUBSCR_SIG_ERR;
                        subscriber_post(appdata->subscribers, proc_result, subsig, NULL, 0);
                    }
                    else {
                        // Raw or Unidentified Message received
                        proc_result = fmt_printhex((uint8_t*)putsbuf, &putsbytes, &msg, msgbytes, 16);
                        rxstat_fmt  = DFMT_Text;
                    }

                    ///@todo add rx address of input packet (set to 0)
                    dterm_output_rxstat(dth, rxstat_fmt, putsbuf, putsbytes, 0, rpkt->sequence, rpkt->tstamp, rpkt->crcqual);
                    
                    // Recalculate message size following the treatment of the last segment
                    msgbytes -= (msg - lastmsg);
                }
            }
            
            // Remove the packet that was just received
            pktlist_del(appdata->rlist, rpkt);
        }
        
        modbus_parser_END:
        pthread_mutex_unlock(appdata->tlist_mutex);
        pthread_mutex_unlock(appdata->rlist_mutex);
        pthread_mutex_unlock(dth->iso_mutex);
        
        ///@todo Can check for major error in pkt_condition
        ///      Major errors are integers less than -1
        
    } // END OF WHILE()
    
    modbus_parser_TERM:
    
    /// This code should never occur, given the while(1) loop.
    /// If it does (possibly a stack fuck-up), we print this "chaotic error."
    fprintf(stderr, "\n--> Chaotic error: modbus_parser() thread broke loop.\n");
    raise(SIGINT);
    return NULL;
}




#endif  // defined(MODBUS_ENABLED)

