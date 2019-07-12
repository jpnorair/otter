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

// Local Libraries/Includes
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




static int sub_intf_bitsperbyte(mpipe_intf_t* intf) {
    int bitsperbyte = 8;

    if (intf->params != NULL) {
        switch (intf->type) {
            case MPINTF_tty: {
                mpipe_tty_t* ttyinfo = (mpipe_tty_t*)intf->params;
                bitsperbyte = ttyinfo->data_bits + ttyinfo->parity + ttyinfo->stop_bits;
            } break;
                
            default: break;
        }
    }
    
    return bitsperbyte;
}


static int sub_intf_baudrate(mpipe_intf_t* intf) {
/// Default baudrate is 10 Mbps
    int baudrate = 10000000;

    if (intf->params != NULL) {
        switch (intf->type) {
            case MPINTF_tty: {
                mpipe_tty_t* ttyinfo = (mpipe_tty_t*)intf->params;
                baudrate = ttyinfo->baud;
            } break;
                
            default: break;
        }
    }
    
    return baudrate;
}


static int sub_ttybaudrate(int native_baud) {
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


static void sub_freeparams(mpipe_intf_t* mpintf) {
    if (mpintf != NULL) {
        if (mpintf->params != NULL) {
            switch (mpintf->type) {
            case MPINTF_tty:
                if ( ((mpipe_tty_t*)mpintf->params)->path != NULL) {
                    free(((mpipe_tty_t*)mpintf->params)->path);
                }
                break;
                
            default: break;
            }
            
            free(mpintf->params);
            mpintf->params = NULL;
        }
        
        mpintf->type = MPINTF_null;
    }
}


static int sub_check_handle(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table = (mpipe_tab_t*)handle;
    int rc = -1;
    
    if (table != NULL) {
        if ((id >= 0) && (id < table->size)) {
            rc = id;
        }
    }

    return rc;
}











int mpipe_init(mpipe_handle_t* handle, size_t num_intf) {
/// Initialize the interface table based on the num_intf parameter.  If it's
/// zero then it is considered to be 1.
    mpipe_tab_t* table;
    
    if ((handle == NULL) || (num_intf == 0)) {
        return -1;
    }
    
    table = malloc(sizeof(mpipe_tab_t));
    if (table == NULL) {
        return -2;
    }
    
    table->size = num_intf;
    table->intf = calloc(num_intf, sizeof(mpipe_intf_t));
    if (table->intf == NULL) {
        free(table);
        return -3;
    }
    
    for (int i=0; i<table->size; i++) {
        table->intf[i].type     = MPINTF_null;
        table->intf[i].params   = NULL;
        table->intf[i].fd.in    = -1;
        table->intf[i].fd.out   = -1;
    }

    *handle = (mpipe_handle_t)table;
    return 0;
}



void mpipe_deinit(mpipe_handle_t handle) {
    mpipe_tab_t* table;
    
    if (handle != NULL) {
        table = (mpipe_tab_t*)handle;
        
        if (table->intf != NULL) {
            while (table->size > 0) {
                table->size--;
                mpipe_close(handle, (int)table->size);
                sub_freeparams(&table->intf[table->size]);
            }
            free(table->intf);
        }
        free(table);
    }
}



int mpipe_pollfd_alloc(mpipe_handle_t handle, struct pollfd** pollitems, short pollevents) {
    mpipe_tab_t* table;
    
    if ((pollitems == NULL) || (handle == NULL)) {
        return -1;
    }
    
    table = (mpipe_tab_t*)handle;
    
    *pollitems = calloc(table->size, sizeof(struct pollfd));
    if (pollitems == NULL) {
        return -2;
    }
    
    for (int i=0; i<table->size; i++) {
        (*pollitems)[i].fd     = table->intf[i].fd.in;
        (*pollitems)[i].events = pollevents;
    }
    
    return (int)(table->size);
}



size_t mpipe_numintf_get(mpipe_handle_t handle) {
    mpipe_tab_t* table = (mpipe_tab_t*)handle;
    size_t tabsize = 0;

    if (table != NULL) {
        tabsize = table->size;
    }
    
    return tabsize;
}


mpipe_fd_t* mpipe_fds_get(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table;
    mpipe_fd_t* fds = NULL;
    
    if (sub_check_handle(handle, id) >= 0) {
        table = (mpipe_tab_t*)handle;
        if (table->intf[id].fd.in >= 0) {
            fds = &table->intf[id].fd;
        }
    }
    
    return fds;
}

///@todo DEBUG: this function seems to return NULL in modbus_reader/mpipe_reader
const char* mpipe_file_get(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table;
    const char* output = NULL;

    if (sub_check_handle(handle, id) >= 0) {
        table = (mpipe_tab_t*)handle;
        output = mpipe_file_resolve(&table->intf[id]);
    }

    return output;
}


void* mpipe_intf_get(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table = handle;
    void* intf = NULL;
    
    if (sub_check_handle(handle, id) >= 0) {
        intf = &table->intf[id];
    }
    return intf;
}


void* mpipe_intf_fromfile(mpipe_handle_t handle, const char* file) {
///@note there's no indexing here, because it's not expected for otter to
/// handle more than 4 or 5 interfaces at most.
    mpipe_tab_t* table = handle;
    void* intf = NULL;

    if (handle != NULL) {
        for (int i=0; i<table->size; i++) {
            const char* stored_file;
            switch (table->intf[i].type) {
                case MPINTF_tty: stored_file = ((mpipe_tty_t*)table->intf[i].params)->path;
                    break;
                default: stored_file = NULL;
                    break;
            }
            if (strcmp(stored_file, file) == 0) {
                intf = (void*)&table->intf[i];
                break;
            }
        }
    }
    
    return intf;
}


mpipe_fd_t* mpipe_fds_resolve(void* intfp) {
    mpipe_intf_t* intf = (mpipe_intf_t*)intfp;
    if (intf != NULL) {
        return &intf->fd;
    }
    return NULL;
}


const char* mpipe_file_resolve(void* intfp) {
    mpipe_intf_t* intf = (mpipe_intf_t*)intfp;
    const char* output = NULL;
    
    if (intf != NULL) {
        switch (intf->type) {
        case MPINTF_tty: output = ((mpipe_tty_t*)intf->params)->path;
            break;
        default:
            break;
        }
    }
    return output;
}


int mpipe_id_resolve(mpipe_handle_t handle, void* intfp) {
    int id;

    if (handle != NULL) {
        int max     = (int)((mpipe_tab_t*)handle)->size;
        int delta   = (int)(intfp - (void*)((mpipe_tab_t*)handle)->intf);
        
        if ((delta % sizeof(mpipe_intf_t)) == 0) {
            id = delta / sizeof(mpipe_intf_t);
            if (id < max) {
                return id;
            }
        }
    }
    return -1;
}



void mpipe_writeto_intf(void* intf, uint8_t* data, int data_bytes) {
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




/** MPipe Control Functions
  * ========================================================================<BR>
  * Typically called by main() to setup the TTY file used by MPipe.
  */
static int sub_opentty(mpipe_intf_t* ttyintf) {
    mpipe_tty_t* ttyparams = (mpipe_tty_t*)ttyintf->params;
    struct termios tio;
    int i_par;
    int rc = 0;

    // first open with O_NDELAY
    ttyintf->fd.in = open(ttyparams->path, O_RDWR | O_NDELAY | O_EXCL);
    if (ttyintf->fd.in < 0 ) {
        rc = -1;
        goto sub_opentty_EXIT;
    }
    
    // TTY device has output and input on the same file descriptor
    ttyintf->fd.out = ttyintf->fd.in;
    
    // then reset O_NDELAY
    if ( fcntl(ttyintf->fd.in, F_SETFL, O_RDWR) )  {
        rc = -2;
        goto sub_opentty_EXIT;
    }
    
    // clear the datastruct just in case
    bzero(&tio, sizeof(struct termios));
    
    // Framing parameters are derived in mpipe_opentty()
    ///@todo Currently ignores parity on RX (IGNPAR)
    i_par           = IGNPAR;
    tio.c_cflag     = ttyparams->data_bits | ttyparams->stop_bits | ttyparams->parity | CREAD | CLOCAL | ttyparams->flowctl;
    tio.c_iflag     = IGNBRK | i_par;
    tio.c_oflag     = CR0 | TAB0 | BS0 | VT0 | FF0;
    tio.c_lflag     = 0;
    
    // VMIN = 0, VTIME = 1: Works for systems where poll() is too slow (some Linuxes)
    // VMIN = 1, VTIME = 0: Works for systems where poll() actually works (any BSD, some Linuxes)
#   if (OTTER_FEATURE_NOPOLL)
        tio.c_cc[VMIN]  = 1;        // smallest read is one character
        tio.c_cc[VTIME] = 0;        // Inter-character timeout (after VMIN) is 0.1sec
#   else
        tio.c_cc[VMIN]  = 1;        // smallest read is one character
        tio.c_cc[VTIME] = 0;        // There is no inter-character timeout
#   endif
    
    tcflush( ttyintf->fd.in, TCIFLUSH );
    tcflush( ttyintf->fd.out, TCOFLUSH );
    cfsetospeed(&tio, ttyparams->baud);
    cfsetispeed(&tio, ttyparams->baud);
    
    // Using TCSANOW will do [something]
    if (tcsetattr(ttyintf->fd.in, TCSANOW, &tio) != 0)  {
        rc = -3;
        goto sub_opentty_EXIT;
    }
    
    // RTS/CTS are not available at this moment (may never be)
    //if( flowctrl != FLOW_HW )  {
    //    if (rts)    tiocmbis(table->intf[id].fd.in,TIOCM_RTS);
    //    else        tiocmbic(table->intf[id].fd.in,TIOCM_RTS);
    //}
    
    // DTR is not available at this moment (may never be)
    //if (dtr)    tiocmbis(table->intf[id].fd.in,TIOCM_DTR);
    //else        tiocmbic(table->intf[id].fd.in,TIOCM_DTR);
    
    sub_opentty_EXIT:
    return rc;
}



int mpipe_opentty( mpipe_handle_t handle, int id,
                const char *dev, int baud, 
                int data_bits, char parity, int stop_bits, 
                int flowctrl, int dtr, int rts    ) {

    mpipe_tab_t* table;
    mpipe_tty_t* ttyparams;
    int rc = 0;
    
    // Input Check: null values
    if (sub_check_handle(handle, id) < 0) {
        return -1;
    }
    
    table = (mpipe_tab_t*)handle;

    // Input Check: TTY has a valid name resembling a serial line filename
    ///@todo This routine could be made more thorough and cross platform
    if (strncmp(dev, "/dev/", 5) < 0) {
        fprintf( stderr, "File %s is not a suitable device file\n", dev );
        return -1;
    }

    /// Free parameters for old interface.  New ones will be allocated for TTY.
    /// Parameters can be different for different interface types.
    sub_freeparams(&table->intf[id]);

    table->intf[id].params = malloc(sizeof(mpipe_tty_t));
    if (table->intf[id].params == NULL) {
        rc = -2;
        goto mpipe_opentty_EXIT;
    }

    table->intf[id].type    = MPINTF_tty;
    ttyparams               = table->intf[id].params;
    ttyparams->path         = calloc(strlen(dev)+1, sizeof(char));
    if (ttyparams->path == NULL) {
        rc = -3;
        goto mpipe_opentty_EXIT;
    }

    strcpy(ttyparams->path, dev);

    ttyparams->baud = sub_ttybaudrate(baud);
    if (ttyparams->baud < 0) {
        fprintf(stderr, "Error: baudrate %d is not permitted.  Default baudrate is 115200\n", baud);
        rc = -4;
        goto mpipe_opentty_EXIT;
    }

    switch (data_bits) {
        case 5: ttyparams->data_bits = CS5; break;
        case 6: ttyparams->data_bits = CS6; break;
        case 7: ttyparams->data_bits = CS7; break;
        case 8: ttyparams->data_bits = CS8; break;
       default: fprintf(stderr, "Error: %d bits/byte not permitted.\n", data_bits);
                rc = -5;
                break;
    }

    ttyparams->parity       = (parity == (int)'N') ? 0 : PARENB;
    ttyparams->stop_bits    = (stop_bits == 2) ? CSTOPB : 0;
    
    ///@todo flowctl, dtr, rts settings not yet supported
    ttyparams->flowctl      = flowctrl;
    ttyparams->dtr          = dtr;
    ttyparams->rts          = rts;

    // Make sure all flow control is compatible with MPipe spec (i.e. disabled)
    if ((ttyparams->flowctl != 0) || (ttyparams->dtr != 0) || (ttyparams->rts != 0)) {
        fprintf(stderr, "In current MPipe implementation there is no flow control.  You specifed:\n");
        if (ttyparams->flowctl != 0) {
            fprintf(stderr, " - Flowctrl = %d\n", ttyparams->flowctl);
        }
        if (ttyparams->dtr != 0) {
            fprintf(stderr, " - DTR enabled\n");
        }
        if (ttyparams->rts != 0) {
            fprintf(stderr, " - RTS/CTS enabled\n");
        }
        rc = -6;
        goto mpipe_opentty_EXIT;
    }

    rc = sub_opentty(&table->intf[id]);
    if (rc < 0) {
        switch (rc) {
            case -1: fprintf(stderr, "Error: Cannot open device %s\n", ttyparams->path);
                break;
            case -2: fprintf(stderr, "Error: Cannot fcntl device %s\n", ttyparams->path);
                break;
            case -3: fprintf(stderr, "Can't set mode of serial line %s\n", ttyname(table->intf[id].fd.in));
                break;
            default: break;
        }
        rc = -7;
    }

    mpipe_opentty_EXIT:
    switch (rc) {
        case 0: rc = table->intf[id].fd.in;
                break;

        case -7:
        case -6:
        case -5:
        case -4:
        case -3: sub_freeparams(&table->intf[id]);
        default: break;
    }
    
    return rc;
}


///@todo there may be freeze during this function
int mpipe_reopen(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table = (mpipe_tab_t*)handle;
    int rc = -1;
    
    if (sub_check_handle(handle, id) >= 0) {
        mpipe_close(handle, id);
        
        switch (table->intf[id].type) {
            case MPINTF_tty: rc = sub_opentty(&table->intf[id]);
                break;
            default:
                break;
        }
    }
    
    return rc;
}



int mpipe_close(mpipe_handle_t handle, int id) {
    mpipe_fd_t* fds;
    int rc=0, rci=0, rco=0;
    
    fds = mpipe_fds_get(handle, id);

    if (fds != NULL) {
        if (fds->in > 0) {
            rci = close(fds->in);
        }
        if ((fds->out > 0) && (fds->out != fds->in)) {
            rco = close(fds->out);
        }
        if (rco == 0)   fds->out = -1;
        else            rc = rco;
        if (rci == 0)   fds->in = -1;
        else            rc = rci;
    }
    else {
        rc = -1;
    }
    
    return rc;
}





void mpipe_flush(mpipe_handle_t handle, int id, size_t est_rembytes, int queue_selector) {
    int i, j;

    if (handle != NULL) {
        mpipe_tab_t* table = (mpipe_tab_t*)handle;
        i   = (id < 0) ? 0 : id;
        j   = (id < 0) ? (int)table->size : id+1;
        
        for ( ; (i<j) && (i<table->size); i++) {
#       ifdef _MANUAL_DRAIN
            float       a;
            useconds_t  micros;

            // estimated bits remaining in packet
            a = (float)(est_rembytes * sub_intf_bitsperbyte(&table->intf[i]));
            // estimated seconds remaining in packet
            a /= (float)sub_intf_baudrate(&table->intf[i]);
            // est microseconds
            micros = (useconds_t)(a * 1000000.f);
            usleep(micros);
            if (table->intf[i].type == MPINTF_tty) {
                tcflush(table->intf[i].fd.out, TCOFLUSH);
                tcflush(table->intf[i].fd.in, TCIFLUSH);
            }
#       else
            if (table->intf[i].type == MPINTF_tty) {
                if (queue_selector & TCOFLUSH)  tcdrain(table->intf[i].fd.out);
                if (queue_selector & TCIFLUSH)  tcflush(table->intf[i].fd.in, TCIFLUSH);
            }
#       endif
        }

    }
}


//void mpipe_flush(mpipe_ctl_t* mpctl, size_t est_rembytes, int queue_selector) {
//    float       a;
//    useconds_t  micros;
//
//    a       = (float)(est_rembytes * 10);   // estimated bits remaining in packet
//    a      /= (float)mpctl->baudrate;       // estimated seconds remaining in packet
//    micros  = (useconds_t)(a * 1000000.f);  // est microseconds
//
//    usleep(micros);
//    tcflush(mpctl->tty_fd, queue_selector);
//}



