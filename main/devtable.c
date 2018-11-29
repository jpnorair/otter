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

// Local Headers
#include "otter_cfg.h"
#include "devtable.h"

// HB libraries
#if OTTER_FEATURE(SECURITY)
#   include <oteax.h>
#endif


// Standard libs
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>


typedef struct {
    uint16_t    flags;
    uint16_t    vid;
    uint64_t    uid;
    void*       intf;
#   if OTTER_FEATURE(SECURITY)
    eax_ctx     root;
    eax_ctx     user;
#   endif
} devtab_item_t;

typedef struct {
    uint16_t        vid;
    devtab_item_t*  cell;
} devtab_vid_t;

typedef struct {
    devtab_vid_t*   vdex;
    devtab_item_t** cell;
    size_t          vids;
    size_t          size;
    size_t          alloc;
} devtab_t;







// comapres two values by integer compare
static int sub_cmp64(uint64_t a, uint64_t b);
static int sub_cmp16(uint16_t a, uint16_t b);
static devtab_item_t* sub_searchop_uid(devtab_t* table, uint64_t uid, int operation);
static devtab_vid_t* sub_searchop_vid(devtab_t* table, uint16_t vid, int operation);







int devtab_init(void** new_handle) {
    devtab_t* newtab;

    if (new_handle == NULL) {
        return -1;
    }
    
    newtab = malloc(sizeof(devtab_t));
    if (newtab == NULL) {
        return -2;
    }
    
    
//    newtab->cell = malloc(OTTER_DEVTAB_CHUNK*sizeof(devtab_item_t*));
//    if (newtab->cell == NULL) {
//        free(newtab);
//        return -3;
//    }
//
//    newtab->vdex = calloc(OTTER_DEVTAB_CHUNK, sizeof(devtab_vid_t));
//    if (newtab->vdex == NULL) {
//        free(newtab->cell);
//        free(newtab);
//        return -4;
//    }
    
    newtab->size    = 0;
    newtab->vids    = 0;
    newtab->alloc   = 0;
    
    return 0;
}


void devtab_free(void* handle) {
    devtab_t* table = (devtab_t*)handle;
    int i;
    
    if (table != NULL) {
        if (table->cell != NULL) {
            i = (int)table->size;
            while (--i >= 0) {
                devtab_item_t* item;
                item = table->cell[i];
                if (item != NULL) {
                    free(item);
                }
            }
            free(table->cell);
        }
        if (table->vdex != NULL) {
            free(table->vdex);
        }
        
        free(table);
    }
}




int devtab_list(devtab_t* table, char* dst, size_t dstmax) {
    int i;
    int chars_out = 0;
    for (i=0; i<table->size; i++) {
        chars_out  += (16+1+4+1);
        if (chars_out < dstmax) {
            dst    += sprintf(dst, "%016llX %04X", table->cell[i]->uid, table->cell[i]->vid);
            *dst++  = '\n';
        }
        else {
            break;
        }
    }
    
    return chars_out;
}




int devtab_insert(void* handle, uint64_t uid, uint16_t vid, void* intf_handle, void* rootkey, void* userkey) {
    devtab_item_t* item;
    devtab_vid_t* viditem;
    devtab_t* table = handle;
    
    if (table == NULL) {
        return -1;
    }
    
    ///1. Make sure there is room in the tables
    if (table->alloc <= table->size) {
        devtab_item_t** newtable_cell;
        devtab_vid_t*   newtable_vid;
        size_t          newtable_alloc;
        
        newtable_alloc  = table->alloc + OTTER_DEVTAB_CHUNK;
        if (table->alloc == 0) {
            newtable_cell   = malloc(newtable_alloc * sizeof(devtab_item_t*));
            newtable_vid    = malloc(newtable_alloc * sizeof(devtab_vid_t));
        }
        else {
            newtable_cell   = realloc(table->cell, newtable_alloc * sizeof(devtab_item_t*));
            newtable_vid    = realloc(table->vdex, newtable_alloc * sizeof(devtab_vid_t));
        }
        
        if ((newtable_cell == NULL) || (newtable_vid == NULL)) {
            return -2;
        }
        
        table->cell     = newtable_cell;
        table->alloc    = newtable_alloc;
    }
    
    ///2. Insert a new cmd item,
    item = sub_searchop_uid(table, uid, 1);
    if (item == NULL) {
        return -3;
    }
    
    /// 3. add the vid index, if a VID is supplied, and link to the cell
    if (vid != 0) {
        viditem = sub_searchop_vid(table, vid, 1);
        if (viditem == NULL) {
            return -4;
        }
        viditem->cell = item;
    }
    
    /// 4. Fill-up cell values.
    item->vid   = vid;
    item->intf  = intf_handle;

#   if OTTER_FEATURE(SECURITY)
    if (rootkey != NULL) {
        item->flags |= 1;
        eax_init_and_key((io_t*)rootkey, &item->root);
    }
    if (userkey != NULL) {
        item->flags |= 2;
        eax_init_and_key((io_t*)userkey, &item->user);
    }
#   endif

    return 0;
}



void devtab_remove(void* handle, uint64_t uid) {
    devtab_item_t* item;
    devtab_t* table = handle;
    uint16_t vid;
    
    item = sub_searchop_uid(table, uid, -1);
    if (item != NULL) {
        vid = item->vid;
        free(item);
        sub_searchop_vid(table, vid, -1);
    }
}



void devtab_unlist(void* handle, uint16_t vid) {
    devtab_t* table = handle;
    sub_searchop_vid(table, vid, -1);
}



devtab_node_t devtab_select(void* handle, uint64_t uid) {
    return (devtab_node_t)sub_searchop_uid((devtab_t*)handle, uid, 0);
}


devtab_node_t devtab_select_vid(void* handle, uint16_t vid) {
    return (devtab_node_t)sub_searchop_vid((devtab_t*)handle, vid, 0);
}



uint16_t devtab_get_vid(void* handle, devtab_node_t node) {
    uint16_t vid = OTTER_PARAM_DEFMBSLAVE;
    if (handle != NULL) {
        if (node != NULL) {
            vid = ((devtab_item_t*)node)->vid;
        }
    }
    return vid;
}


void* devtab_get_intf(void* handle, devtab_node_t node) {
    void* intf = NULL;
    if (handle != NULL) {
        if (node != NULL) {
            intf = ((devtab_item_t*)node)->intf;
        }
    }
    return intf;
}


void* devtab_get_rootctx(void* handle, devtab_node_t node) {
#if OTTER_FEATURE(SECURITY)
    void* rootkey = NULL;
    if (handle != NULL) {
        if (node != NULL) {
            rootkey = &((devtab_item_t*)node)->root;
        }
    }
    return rootkey;
#else
    return NULL;
#endif
}


void* devtab_get_userctx(void* handle, devtab_node_t node) {
#if OTTER_FEATURE(SECURITY)
    void* userkey = NULL;
    if (handle != NULL) {
        if (node != NULL) {
            userkey = &((devtab_item_t*)node)->user;
        }
    }
    return userkey;
#else
    return NULL;
#endif
}


uint64_t devtab_lookup_uid(void* handle, uint16_t vid) {
    devtab_node_t node;
    uint64_t uid = 0;
    node = devtab_select_vid(handle, vid);
    if (node != NULL) {
        uid = ((devtab_item_t*)node)->uid;
    }
    return uid;
}


uint16_t devtab_lookup_vid(void* handle, uint64_t uid) {
    return devtab_get_vid(handle, devtab_select(handle, uid));
}


void* devtab_lookup_intf(void* handle, uint64_t uid) {
    return devtab_get_intf(handle, devtab_select(handle, uid));
}


void* devtab_lookup_rootctx(void* handle, uint64_t uid) {
    return devtab_get_rootctx(handle, devtab_select(handle, uid));
}


void* devtab_lookup_userctx(void* handle, uint64_t uid) {
    return devtab_get_userctx(handle, devtab_select(handle, uid));
}








static devtab_item_t* sub_searchop_uid(devtab_t* table, uint64_t uid, int operation) {
/// An important condition of using this function with insert is that table->alloc must
/// be greater than table->size.
///
/// Verify that cmdname is not a zero-length string, then search for it in the
/// list of available commands.
///
    devtab_item_t** head;
    devtab_item_t* output = NULL;
    int cci = 0;
    int csc = -1;

    if (table == NULL) {
        return NULL;
    }

    {   int l   = 0;
        int r   = (int)table->size - 1;
        head    = table->cell;
    
        while (r >= l) {
            cci = (l + r) >> 1;
            csc = sub_cmp64(head[cci]->uid, uid);
            
            switch (csc) {
                case -1: r = cci - 1;
                         break;
                
                case  1: l = cci + 1;
                         break;
                
                default: output = head[cci];
                         goto sub_searchop_uid_DO;
            }
        }
        
        
        sub_searchop_uid_DO:
        
        /// Adding a new devtab_item_t, or removing an existing one
        if ((operation > 0) && (output == NULL)) {
            cci += (csc > 0);
            for (int i=(int)table->size; i>cci; i--) {
                head[i] = head[i-1];
            }
            
            output = malloc(sizeof(devtab_item_t));
            if (output == NULL) {
                return NULL;
            }
            
            output->flags   = 0;
            output->uid     = uid;
            head[cci]       = output;
            table->size++;
        }
        else if ((operation < 0) && (output != NULL)) {
            table->size--;
            for (int i=cci; i<table->size; i++) {
                head[i] = head[i+1];
            }
            output = head[cci];
        }
    }
    
    return output;
}


static devtab_vid_t* sub_searchop_vid(devtab_t* table, uint16_t vid, int operation) {
/// An important condition of using this function with insert is that table->alloc must
/// be greater than table->size.
///
/// Verify that cmdname is not a zero-length string, then search for it in the
/// list of available commands.
///
    devtab_vid_t* head;
    devtab_vid_t* output = NULL;
    int cci = 0;
    int csc = -1;

    if (table == NULL) {
        return NULL;
    }

    {   int l   = 0;
        int r   = (int)table->vids - 1;
        head    = table->vdex;
    
        while (r >= l) {
            cci = (l + r) >> 1;
            csc = sub_cmp16(head[cci].vid, vid);
            
            switch (csc) {
                case -1: r = cci - 1;
                         break;
                
                case  1: l = cci + 1;
                         break;
                
                default: output = &head[cci];
                         goto sub_searchop_vid_DO;
            }
        }
        
        /// Adding a new devtab_vid_t
        /// If there is a matching name (output != NULL), the new one will replace old.
        /// It will adjust the table and add the new item, otherwise.
        sub_searchop_vid_DO:
        if ((operation > 0) && (output == NULL)) {
            cci += (csc > 0);
            for (int i=(int)table->vids; i>cci; i--) {
                head[i] = head[i-1];
            }
            output          = &head[cci];
            head[cci].vid   = vid;
            table->vids++;
        }
        else if ((operation < 0) && (output != NULL)) {
            output->cell->vid = 0;
            table->vids--;
            for (int i=cci; i<table->vids; i++) {
                head[i] = head[i+1];
            }
            output = &head[cci];
        }
    }

    return output;
}


static int sub_cmp64(uint64_t a, uint64_t b) {
    return (a < b) - (a > b);
}

static int sub_cmp16(uint16_t a, uint16_t b) {
    return (a < b) - (a > b);
}



