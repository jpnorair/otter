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

#include <fcntl.h>
#include <errno.h>
#include <assert.h>

// Must be a power of two
#define PPIPE_GROUP_SIZE    16


#define PPIPE_BASEPATH      "./pipes/"


static ppipe_t ppipe = {
    .basepath   = { 0 },
    .fifo       = NULL,
    .num        = 0
};




int sub_assure_path(char* assure_path, mode_t mode) {
    char* p;
    char* file_path = NULL;
    size_t pathlen;
    int rc          = 0;

    assert(assure_path && *assure_path);

    //pathlen     = strlen(assure_path);
    //file_path   = malloc(pathlen+1);
    //if (file_path == NULL) {
    //    return -2;
    //}
    
    ///@note Originally used strcpy, but bizarre side-effects were observed
    /// on assure_path, in certain cases.  Adding manual terminators.
    //strncpy(file_path, assure_path, pathlen);
    //file_path[pathlen]      = 0;
    //assure_path[pathlen]    = 0;
    
    /// Now not using copying at all.  strcpy/strncpy were being chaotic.
    file_path = assure_path;
    
    for (p=strchr(file_path+1, '/'); p!=NULL; p=strchr(p+1, '/')) {
        *p='\0';
        if (mkdir(file_path, mode) == -1) {
            if (errno!=EEXIST) { 
                *p='/'; 
                rc = -1; 
                goto sub_assure_path_END;
            }
        }
        *p='/';
    }
    
    sub_assure_path_END:
    
    //free(file_path);
    return rc;
}





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
    ppipe_fifo_t*   fifo;
    int             ppd;
    size_t          alloc_size;
    int             test_fd;
    int             rc;
    struct stat     st;
    
    int group       = ppipe.num & (PPIPE_GROUP_SIZE-1);
    
    if ((ppipe.num != 0) && (group == 0)) {
        size_t elem = (ppipe.num / PPIPE_GROUP_SIZE) + 1;
        alloc_size  = PPIPE_GROUP_SIZE * sizeof(ppipe_fifo_t);
        ppipe.fifo  = realloc(ppipe.fifo, elem*alloc_size);
        memset(&ppipe.fifo[ppipe.num], 0, alloc_size);
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

    // The "+2" is for the intermediate '/' and the null terminator
    alloc_size  = strlen(ppipe.basepath) + strlen(prefix) + strlen(name) + 2;
    fifo->fpath = malloc(alloc_size);
    if (fifo->fpath == NULL) {
        ppd = -2;
        goto ppipe_new_EXIT;
    }
    
    memset(fifo->fpath, 0, alloc_size);
    strcpy(fifo->fpath, ppipe.basepath); 
    strcat(fifo->fpath, prefix); 
    strcat(fifo->fpath, "/");
    strcat(fifo->fpath, name);
    //fprintf(stderr, "%s %d: %s\n", __FUNCTION__, __LINE__, fifo->fpath);
    
    /// See if FIFO already exists, in which case just open it.  Else, make it.
    rc = access( fifo->fpath, F_OK );
    
    if (rc != -1) {
        test_fd = open(fifo->fpath, O_RDONLY | O_NONBLOCK);
        rc      = fstat(test_fd, &st);
        close(test_fd);
        
        if ((rc == 0) && S_ISFIFO(st.st_mode) && ((st.st_mode&0777)==0666)) {
            // File exists and is fifo
            //printf("File already exists, is fifo, and has proper mode: continuing.\n");
        }
        
        else {
            // File exists, but is not fifo, or some other error
            //printf("File already exists, is not FIFO and has improper mode: exiting.\n");
            ppd = -3;
            goto ppipe_new_FIFOERR;
        }
    }
    
    else { 
        // FIFO does not exist, make it the way we need to.
        umask(0);
        rc = sub_assure_path(fifo->fpath, 0755);
        
        if (rc == 0) {
            rc = mkfifo(fifo->fpath, 0666);
        }
        if (rc != 0) {
            ppd = -4;
            goto ppipe_new_FIFOERR;
        }

        return ppd;
    }
    
    ppipe_new_FIFOERR:
    if (ppd < 0) {
        fprintf(stderr, "Could not make FIFO \"%s\": code=%d\n", fifo->fpath, ppd);
        free(fifo->fpath);
        fifo->fpath = NULL;
    }

    ppipe_new_EXIT:
    return ppd;
}




int ppipe_del(int ppd) {
    ppipe_fifo_t* fifo;
    size_t i = (size_t)ppd;
    
    if (i >= ppipe.num) {
        return -1;
    }
    if (i == (ppipe.num-1)) {
        ppipe.num--;
    }
    
    fifo = &ppipe.fifo[i];
    if (fifo == NULL) {
        return -2;
    }

    if (fifo->fpath != NULL) {
        int rc;
        rc = remove(fifo->fpath);
        if (rc != 0) {
            fprintf(stderr, "Could not remove FIFO \"%s\": code=%d\n", fifo->fpath, rc);
        }
        
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
    
    return NULL;
    //return ppipe.fifo[i].file;
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


