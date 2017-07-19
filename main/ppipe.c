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

#include "ppipe.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define PPIPE_GROUP_SIZE    16
#define PPIPE_BASEPATH      "./"


static ppipe_t ppipe = {
    .basepath   = { 0 },
    .fifo       = NULL,
    .num        = 0
};



int ppipe_init(const char* basepath) {
    size_t alloc_size;

    if (ppipe.num != 0) {
        ppipe_deinit();
    }

    /// Set basepath
    if (basepath == NULL) {
        strcpy(ppipe.basepath, PPIPE_BASEPATH);
        return 0;
    }
    if (strlen(basepath) > 255) {
        return -1;
    }
    strcpy(ppipe.basepath, basepath);
    
    /// Malloc the first group of pipe files
    alloc_size  = PPIPE_GROUP_SIZE * sizeof(ppipe_fifo_t);
    ppipe.fifo  = malloc(alloc_size);
    if (ppipe.fifo == NULL) {
        return -2;
    }
    memset(ppipe.fifo, 0, alloc_size);
    
    return 0;
}



void ppipe_deinit(void) {
/// Go through all pipes, close, delete, free data, and set data to defaults.
    
    for (int i=0; i<ppipe.num; i++) {
        ppipe_del(i);
    }
    
    ppipe.basepath[0]   = 0;
    ppipe.fifo          = NULL;
    ppipe.num           = 0;
}



int ppipe_new(const char* prefix, const char* name, const char* fmode) {
    ppipe_fifo_t* fifo;
    int     ppd;
    size_t  alloc_size;
    FILE* test;
    int test_fd;
    struct stat st;
    int rc;
    
    ///@note user and root rw
    mode_t  mode    = 0x660;
    
    if ((ppipe.num != 0) && ((ppipe.num % PPIPE_GROUP_SIZE) == 0)) {
        ppipe.fifo = realloc(ppipe.fifo, (ppipe.num + PPIPE_GROUP_SIZE));
    }
    if (ppipe.fifo == NULL) {
        ppd = -1;
        goto ppipe_new_EXIT;
    }
    
    ppd = (int)ppipe.num;
    ppipe.num++;
    fifo = &ppipe.fifo[ppd];
    
    if (fifo->fpath != NULL) {
        ppipe_del(ppd);
    }
    
    alloc_size  = strlen(ppipe.basepath) + strlen(prefix) + strlen(name) + 1;
    fifo->fpath = malloc(alloc_size);
    if (fifo->fpath == NULL) {
        ppd = -2;
        goto ppipe_new_EXIT;
    }
    
    strcpy(fifo->fpath, ppipe.basepath);
    strcat(fifo->fpath, prefix);
    strcat(fifo->fpath, name);
    
    /// See if FIFO already exists, in which case just open it.  Else, make it.
    test = fopen(fifo->fpath, fmode);
    if (test != NULL) {
        // File already exists
        test_fd = fileno(test);
        rc      = fstat(test_fd, &st);
        
        if ((rc == 0) && S_ISFIFO(st.st_mode)) {
            // File exists and is fifo
            //fflush(test);
            fifo->file  = test;
            fifo->fd    = test_fd;
        }
        
        else {
            // File exists, but is not fifo, or some other error
            ppd = -3;
            goto ppipe_new_FIFOERR;
        }
    }
    
    else {
        // File does not exist, make it.
        if (mkfifo(fifo->fpath, mode) != 0) {
            ppd = -4;
            goto ppipe_new_FIFOERR;
        }
        
        // FIFO is now created, and we open it.
        fifo->file = fopen(fifo->fpath, fmode);
        if (fifo->file != NULL) {
            fifo->fd = fileno(fifo->file);
            return ppd;
        }
    }
    
    ppipe_new_FIFOERR:
    free(fifo->fpath);
    fifo->fpath = NULL;
    fifo->file  = NULL;
    fifo->fd    = -1;
    
    ppipe_new_EXIT:
    return ppd;
}




int ppipe_del(int ppd) {
    ppipe_fifo_t* fifo;
    size_t i = (size_t)ppd;
    
    if (i <= ppipe.num) {
        return -1;
    }
    if (i == (ppipe.num-1)) {
        ppipe.num = (ppipe.num-1);
    }
    
    fifo = &ppipe.fifo[i];
    if (fifo == NULL) {
        return -2;
    }
    if (fifo->file != NULL) {
        fclose(fifo->file);
        fifo->file  = NULL;
        fifo->fd    = -1;
    }
    if (fifo->fpath != NULL) {
        remove(fifo->fpath);
        free(fifo->fpath);
        fifo->fpath = NULL;
    }
    
    return 0;
}



FILE* ppipe_getfile(int ppd) {
    size_t i = (size_t)ppd;

    if (i <= ppipe.num) {
        return NULL;
    }
    
    return ppipe.fifo[i].file;
}



const char* ppipe_getpath(int ppd) {
    size_t i = (size_t)ppd;
    
    if (i <= ppipe.num) {
        return NULL;
    }
    return ppipe.fifo[i].fpath;
}


ppipe_t* ppipe_ref(void) {
    return &ppipe;
}


