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
/** 
  * 
  * @description ppipelist module maintains a list of ppipes.  In fact, it's
  *              just a different API for the ppipe array.  It requires ppipe.
  *              ppipelist is searchable.
  *
  * @note the present implementation utilizes only the ppipe data structure and
  *       performs linear search on the dataset, without an index.  In later
  *       implementations, an index will be created and binary search (or like)
  *       will be implemented within ppipelist.
  */


#include "ppipelist.h"
#include "ppipe.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>


int ppipelist_init(const char* basepath) {
///@todo do what's necessary to alloc ppipelist data

    return ppipe_init(basepath);
}

void ppipelist_deinit(void) {
///@todo do what's necessary to alloc ppipelist data
    return ppipe_deinit();
}


int sub_addpipe(const char* prefix, const char* name, const char* fmode) {
    int rc;
    ppipe_t*        ppipe;
    ppipe_fifo_t*   newfifo;
    
    rc = ppipe_new(prefix, name, fmode);
    if (rc == 0) {
        ppipe = ppipe_ref();
        if (ppipe != NULL) {
            newfifo = &ppipe->fifo[ppipe->num-1];
            
            ///@todo add the node to index
        }
    }
    
    return rc;
}


#ifdef USE_BIDIRECTIONAL_PIPES
int ppipelist_new(const char* prefix, const char* name, const char* fmode) {
    int rc = 0;
    unsigned int    flags;
    char*           namebuf;
    size_t          namelen;
    
    /// Produce the extended pipe name
    /// "x" is a placeholder that gets switched for 'r' and 'w'
    namelen = strlen(name);
    namebuf = malloc(namelen + 8);
    if (namebuf == NULL) {
        rc = -1;
        goto ppipelist_new_END;
    }
    strcpy(namebuf, name);
    strcat(namebuf, ".x");
    namelen += 1;
    
    if (strchr(fmode, '+') != NULL){
        flags = 3;
    }
    else {
        flags   = (strchr(fmode, 'r') != NULL); // read flag
        flags  |= (strchr(fmode, 'w') != NULL); // write flag
    }
    
    // Read fifo
    if (flags & 1) {
        namebuf[namelen] = 'r';
        rc = sub_addpipe(prefix, namebuf, "r");
    }
    if ((rc == 0) && (flags & 2)) {
        namebuf[namelen] = 'w';
        sub_addpipe(prefix, namebuf, "w");
    }

    ppipelist_new_END:
    free(namebuf);
    return rc;
}

#else
int ppipelist_new(const char* prefix, const char* name, const char* fmode) {
    int rc;
    ppipe_t*        ppipe;
    ppipe_fifo_t*   newfifo;
    
    rc = ppipe_new(prefix, name, fmode);
    if (rc == 0) {
        ppipe = ppipe_ref();
        if (ppipe != NULL) {
            newfifo = &ppipe->fifo[ppipe->num-1];
            
            ///@todo add the node to index
        }
    }
    
    return rc;
}

#endif


int ppipelist_del(const char* prefix, const char* name) {
    ppipe_fifo_t*   delfifo;
    int             ppd;
    
    ppd = ppipelist_search(&delfifo, prefix, name);
    
    if (ppd >= 0) {
        ppipe_del(ppd);
        
        ///@todo remove from ppipelist
    }
    
    return ppd;
}


#ifdef USE_BIDIRECTIONAL_PIPES
int ppipelist_search(ppipe_fifo_t** dst, const char* prefix, const char* name, const char* mode) {
/// Linear search of ppipe array.  This will need to be replaced with an
/// indexed search at some point.
    ppipe_t*    base = ppipe_ref();
    int         ppd;
    
    ppd = (int)base->num - 1;
    
    while (ppd >= 0) {
        ppipe_fifo_t* fifo = &base->fifo[ppd];
        int prefix_size = (int)strlen(prefix);
    
        if (strncmp(fifo->fpath, prefix, prefix_size) == 0) {
            size_t name_size = strlen(name);
            if (strncmp(&fifo->fpath[prefix_size+1], name, name_size) == 0) {
                if (fifo->fpath[prefix_size+1+name_size+1] == mode[0]) {
                    *dst = fifo;
                    return ppd;
                }
            }
        }
        ppd--;
    }
    
    *dst = NULL;
    return ppd;
}

#else
int ppipelist_search(ppipe_fifo_t** dst, const char* prefix, const char* name) {
/// Linear search of ppipe array.  This will need to be replaced with an
/// indexed search at some point.
    ppipe_t*    base = ppipe_ref();
    int         ppd;
    
    ppd = (int)base->num - 1;
    
    while (ppd >= 0) {
        ppipe_fifo_t* fifo = &base->fifo[ppd];
        int prefix_size = (int)strlen(prefix);
    
        if (strncmp(fifo->fpath, prefix, prefix_size) == 0) {
            if (strcmp(&fifo->fpath[prefix_size+1], name) == 0) {
                *dst = fifo;
                return ppd;
            }
        }
        ppd--;
    }
    
    *dst = NULL;
    return ppd;
}
#endif



int sub_put(const char* prefix, const char* name, uint8_t* hdr, uint8_t* src, size_t size) {
/// Process for opening and writing to FIFO involves doing a test open in 
/// non-blocking mode to make sure there is a consumer for the write.
/// Header (hdr) is 3 bytes and ignored if NULL.
    ppipe_fifo_t*   fifo;
    int             fd;
    
#   ifdef USE_BIDIRECTIONAL_PIPES
    ppipelist_search(&fifo, prefix, name, "w");
#   else
    ppipelist_search(&fifo, prefix, name);
#   endif

    if (fifo != NULL) {
        //errno = 0;
        fd = open(fifo->fpath, O_WRONLY|O_NONBLOCK);
        if (fd > 0) {
            close(fd);
            fd = open(fifo->fpath, O_WRONLY);
            
            if (hdr != NULL) {
                write(fd, hdr, 3);
            }
            write(fd, src, size);
            close(fd);
            return 0;
        }
    }
    
    return -1;
}


int ppipelist_putbinary(const char* prefix, const char* name, uint8_t* src, size_t size) {
    uint8_t hdr[3];
    hdr[0]  = 0;
    hdr[1]  = (size >> 8) & 0xFF;
    hdr[2]  = (size >> 0) & 0xFF;
    
    return sub_put(prefix, name, hdr, src, size);
}



int ppipelist_puttext(const char* prefix, const char* name, char* src, size_t size) {
    return sub_put(prefix, name, NULL, (uint8_t*)src, size);
}



uint8_t* sub_gethex(uint8_t* dst, uint8_t input) {
    static const char convert[] = "0123456789ABCDEF";
    dst[0]  = convert[input >> 4];
    dst[1]  = convert[input & 0x0f];
    
    return dst;
}

int ppipelist_puthex(const char* prefix, const char* name, char* src, size_t size) {
/// This is a variant of sub_put()
    ppipe_fifo_t*   fifo;
    int             fd;

#   ifdef USE_BIDIRECTIONAL_PIPES
    ppipelist_search(&fifo, prefix, name, "w");
#   else
    ppipelist_search(&fifo, prefix, name);
#   endif

    if (fifo != NULL) {
        //errno = 0;
        fd = open(fifo->fpath, O_WRONLY|O_NONBLOCK);
        
        if (fd > 0) {
            close(fd);
            fd = open(fifo->fpath, O_WRONLY);
            while (size-- != 0) {
                uint8_t hexbuf[2];
                write(fd, sub_gethex(hexbuf, *src++), 2);
            }
            
            close(fd);
            return 0;
        }
    }
    
    return -1;
}






