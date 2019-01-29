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


typedef enum {
    MPINTF_null   = 0,
    MPINTF_tty,
    MPINTF_MAX
} mpipe_intf_enum;

typedef struct {
    char* path;
    int baud;
    int data_bits;
    int parity;
    int stop_bits;
    int flowctl;
    int dtr;
    int rts;
} mpipe_tty_t;

typedef struct {
    mpipe_intf_enum type;
    void*           params;
    mpipe_fd_t      fd;
} mpipe_intf_t;

typedef struct {
    mpipe_intf_t*   intf;
    size_t          size;
} mpipe_tab_t;





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
        switch (mpintf->type) {
            case MPINTF_tty: {
                mpipe_tty_t* ttyparams = mpintf->params;
                if (ttyparams->path != NULL) {
                    free(ttyparams->path);
                }
            } break;
                
            default: break;
        }
        
        free(mpintf->params);
        mpintf->type = MPINTF_null;
    }
}


static int sub_check_handle(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table = (mpipe_tab_t*)handle;
    int rc;
    
    if (table != NULL) {
        rc = 0 - ((id < 0) || (id >= table->size));
    }
    else {
        rc = -1;
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



int mpipe_pollfd_alloc(mpipe_handle_t handle, struct pollfd* pollitems, short pollevents) {
    mpipe_tab_t* table;
    
    if ((pollitems == NULL) || (handle == NULL)) {
        return -1;
    }
    
    table = (mpipe_tab_t*)handle;
    
    pollitems = calloc(table->size, sizeof(struct pollfd));
    if (pollitems == NULL) {
        return -2;
    }
    
    for (int i=0; i<table->size; i++) {
        pollitems[i].fd     = table->intf[i].fd.in;
        pollitems[i].events = pollevents;
    }
    
    return (int)(table->size);
}



mpipe_fd_t* mpipe_fds_get(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table;
    mpipe_fd_t* fds = NULL;
    
    if (sub_check_handle(handle, id) == 0) {
        table = (mpipe_tab_t*)handle;
        if (table->intf[id].fd.in >= 0) {
            fds = &table->intf[id].fd;
        }
    }
    
    return fds;
}


const char* mpipe_file_get(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table;
    const char* output = NULL;
    
    if (sub_check_handle(handle, id) == 0) {
        table = (mpipe_tab_t*)handle;
        switch (table[id].intf->type) {
            case MPINTF_tty: output = ((mpipe_tty_t*)table[id].intf->params)->path;
                break;
            default:
                break;
        }
    }
    
    return output;
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
    if( (ttyintf->fd.in = open( ttyparams->path, O_RDWR | O_NDELAY | O_EXCL )) < 0 ) {
        rc = -1;
        goto sub_opentty_EXIT;
    }
    
    // TTY device has output and input on the same file descriptor
    ttyintf->fd.out = ttyintf->fd.in;
    
    // then reset O_NDELAY
    if( fcntl( ttyintf->fd.in, F_SETFL, O_RDWR ) )  {
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
    tio.c_cc[VMIN]  = 1;        // smallest read is one character... could be changed here
    tio.c_cc[VTIME] = 0;        // no timeout
    
    tcflush( ttyintf->fd.in, TCIFLUSH );
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
    if (sub_check_handle(handle, id) != 0) {
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
    
    ttyparams->baud = sub_ttybaudrate(ttyparams->baud);
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



int mpipe_reopen(mpipe_handle_t handle, int id) {
    mpipe_tab_t* table = (mpipe_tab_t*)handle;
    int rc = -1;
    
    if (sub_check_handle(handle, id) == 0) {
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
    float       a;
    useconds_t  micros;
    
    if (handle != NULL) {
        mpipe_tab_t* table = (mpipe_tab_t*)handle;
        i   = (id < 0) ? 0 : id;
        j   = (id < 0) ? (int)table->size : id+1;
        
        for ( ; (i<j) && (i<table->size); i++) {
            
            // estimated bits remaining in packet
            a = (float)(est_rembytes * sub_intf_bitsperbyte(&table->intf[i]));
            
            // estimated seconds remaining in packet
            a /= (float)sub_intf_baudrate(&table->intf[i]);
            
            // est microseconds
            micros = (useconds_t)(a * 1000000.f);
            
            usleep(micros);
            if (table->intf[i].type == MPINTF_tty) {
                tcflush(table->intf[i].fd.out, queue_selector);
            }
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



