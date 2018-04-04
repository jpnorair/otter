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
    baud            = mpipe_get_baudrate(baud);
    if (baud < 0) {
        fprintf(stderr, "baudrate %d is not permitted.  Default baudrate is 115200\n", mpctl->baudrate);
        return -1;
    }
    
    // Make sure byte encoding is compatible with MPipe spec
//     if ((data_bits != 8) || (parity != 'N') || (stop_bits != 1)) {
//         fprintf(stderr, "In current MPipe implementation, byte encoding MUST be 8N1.  You specifed: %d%c%d\n", 
//                     data_bits, parity, stop_bits);
//         return -1;
//     }
    
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



int mpipe_get_baudrate(int native_baud) {
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



