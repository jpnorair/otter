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
#include "mpipe.h"

// HB libraries
#if OTTER_FEATURE(SECURITY)
#   include <oteax.h>
#endif

#include "../test/test.h"

///@todo bring some of these utils out of cmds dir
#include "../cmds/cmdutils.h"


// Standard libs
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

///@note devtab_item_t is the same as devtab_endpoint_t, for the time being.
//typedef struct {
//    uint16_t    flags;
//    uint16_t    vid;
//    uint64_t    uid;
//    void*       intf;
//    void*       root;
//    void*       user;
//} devtab_item_t;
typedef devtab_endpoint_t devtab_item_t;

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
    pthread_mutex_t access_mutex;
} devtab_t;







// comapres two values by integer compare
static int sub_cmp64(uint64_t a, uint64_t b);
static int sub_cmp16(uint16_t a, uint16_t b);
static devtab_item_t* sub_searchop_uid(devtab_t* table, uint64_t uid, int operation);
static devtab_vid_t* sub_searchop_vid(devtab_t* table, uint16_t vid, int operation);
static int sub_editop(devtab_t* table, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey, int operation);
static int sub_edit_item(devtab_item_t* item, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey);





int devtab_init(devtab_handle_t* new_handle) {
    devtab_t* newtab;

    if (new_handle == NULL) {
        return -1;
    }
    
    newtab = malloc(sizeof(devtab_t));
    if (newtab == NULL) {
        return -2;
    }
    
    if (pthread_mutex_init(&newtab->access_mutex, NULL) != 0) {
        free(newtab);
        return -3;
    }
    
    newtab->size    = 0;
    newtab->vids    = 0;
    newtab->alloc   = 0;
    
    *new_handle     = newtab;
    
    return 0;
}


void devtab_free(devtab_handle_t handle) {
    devtab_t* table = (devtab_t*)handle;
    int i;
    
    if (table != NULL) {
        if (pthread_mutex_lock(&table->access_mutex) != 0) {
            return;
        }
    
        if (table->cell != NULL) {
            i = (int)table->size;
            while (--i >= 0) {
                devtab_item_t* item;
                item = table->cell[i];
                if (item != NULL) {
                    if (item->rootctx != NULL) free(item->rootctx);
                    if (item->userctx != NULL) free(item->userctx);
                    free(item);
                }
            }
            free(table->cell);
        }
        if (table->vdex != NULL) {
            free(table->vdex);
        }
        
        pthread_mutex_unlock(&table->access_mutex);
        pthread_mutex_destroy(&table->access_mutex);
        
        free(table);
    }
}




int devtab_list(devtab_handle_t handle, char* dst, size_t dstmax) {
    static const char* yes = "yes";
    static const char* no = "no";
    int i;
    int chars_out = 0;
    devtab_t* table = handle;
    
    if (table == NULL) {
        return -1;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return -2;
    }
    
    for (i=0; i<table->size; i++) {
        chars_out  += (16+1+4+1);
        if (chars_out < dstmax) {
            char uidstr[17];
            
            cmdutils_uint8_to_hexstr(uidstr, (uint8_t*)&table->cell[i]->uid, 8);

            dst += sprintf(dst, "%i. %s [vid:%i] [root:%s] [user:%s] [intf:%s]\n",
                        i+1,
                        uidstr,
                        table->cell[i]->vid,
                        (table->cell[i]->rootctx == NULL) ? no : yes,
                        (table->cell[i]->userctx == NULL) ? no : yes,
                        mpipe_file_resolve(table->cell[i]->intf)
                    );
        }
        else {
            break;
        }
    }
    
    pthread_mutex_unlock(&table->access_mutex);
    
    return chars_out;
}




int devtab_insert(devtab_handle_t handle, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey) {
    devtab_t* table = handle;
    int rc = 0;
    
    if (table == NULL) {
        return -1;
    }

    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return -2;
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

        table->vdex     = newtable_vid;
        table->cell     = newtable_cell;
        table->alloc    = newtable_alloc;
    }

    ///2. Insert a new cmd item,
    rc = sub_editop(handle, uid, vid, intfp, rootkey, userkey, 1);
    
    pthread_mutex_unlock(&table->access_mutex);
    return rc;
}



int devtab_edit(devtab_handle_t handle, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey) {
    devtab_t* table = handle;
    int rc;
    if (table == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&table->access_mutex);
    rc = sub_editop(handle, uid, vid, intfp, rootkey, userkey, 0);
    pthread_mutex_unlock(&table->access_mutex);
    
    return rc;
}



int devtab_edit_item(devtab_handle_t handle, devtab_node_t node, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey) {
    devtab_t* table = handle;
    int rc;
    if ((table == NULL) || (node == NULL)) {
        return -1;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return -2;
    }
    
    rc = sub_edit_item(node, uid, vid, intfp, rootkey, userkey);
    pthread_mutex_unlock(&table->access_mutex);
    
    return rc;
}



int devtab_remove(devtab_handle_t handle, uint64_t uid) {
    devtab_item_t* item;
    devtab_t* table = handle;
    uint16_t vid;
    
    if (handle == NULL) {
        return -1;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return -2;
    }
    
    item = sub_searchop_uid(table, uid, -1);
    if (item != NULL) {
        vid = item->vid;
        if (item->rootctx != NULL) free(item->rootctx);
        if (item->userctx != NULL) free(item->userctx);
        free(item);
        sub_searchop_vid(table, vid, -1);
    }
    
    pthread_mutex_unlock(&table->access_mutex);
    
    return 0 - (item == NULL);
}



int devtab_unlist(devtab_handle_t handle, uint16_t vid) {
    devtab_t* table = handle;
    devtab_vid_t* item;
    
    if (handle == NULL) {
        return -1;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return -2;
    }
    
    item = sub_searchop_vid(table, vid, -1);
    pthread_mutex_unlock(&table->access_mutex);
    
    return 0 - (item == NULL);
}



devtab_node_t devtab_select(devtab_handle_t handle, uint64_t uid) {
    devtab_t* table = handle;
    devtab_node_t node;
    if (table == NULL) {
        return NULL;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return NULL;
    }
    
    node = (devtab_node_t)sub_searchop_uid(table, uid, 0);
    pthread_mutex_unlock(&table->access_mutex);
    
    return node;
}


devtab_node_t devtab_select_vid(devtab_handle_t handle, uint16_t vid) {
    devtab_t* table = handle;
    devtab_vid_t* viditem;
    devtab_node_t node;
    if (table == NULL) {
        return NULL;
    }
    
    if (pthread_mutex_lock(&table->access_mutex) != 0) {
        return NULL;
    }
    
    viditem = sub_searchop_vid((devtab_t*)handle, vid, 0);
    if (viditem != NULL) {
        node = (devtab_node_t)viditem->cell;
    }
    else {
        node = NULL;
    }

    pthread_mutex_unlock(&table->access_mutex);
    
    return node;
}


devtab_endpoint_t* devtab_resolve_endpoint(devtab_node_t node) {
    return (devtab_endpoint_t*)node;
}


int devtab_validate_usertype(devtab_node_t* node, int userindex) {
    devtab_endpoint_t* endpoint;

    endpoint = devtab_resolve_endpoint(node);
    if (endpoint == NULL) {
        return -1;
    }

    if ((USER_Type)userindex == USER_root) {
        if (endpoint->rootctx == NULL) {
            return -2;
        }
    }

    if ((USER_Type)userindex == USER_user) {
        if (endpoint->userctx == NULL) {
            return -2;
        }
    }

    return 0;
}



typedef enum {
    DEVTAB_Flags,
    DEVTAB_vid,
    DEVTAB_uid,
    DEVTAB_intf,
    DEVTAB_rootctx,
    DEVTAB_userctx
} DEVTAB_Item;

static void* sub_get_item(devtab_t* table, devtab_item_t* node, DEVTAB_Item item) {
    void* get_item = NULL;

    if ((table != NULL) && (node != NULL)) {
        if (pthread_mutex_lock(&table->access_mutex) != 0) {
            return 0;
        }
        switch (item) {
        case DEVTAB_Flags:      get_item = &node->flags;    break;
        case DEVTAB_vid:        get_item = &node->vid;      break;
        case DEVTAB_uid:        get_item = &node->uid;      break;
        case DEVTAB_intf:       get_item = &node->intf;     break;
        case DEVTAB_rootctx:    get_item = &node->rootctx;  break;
        case DEVTAB_userctx:    get_item = &node->userctx;  break;
        default:                get_item = NULL;            break;
        }
        pthread_mutex_unlock(&table->access_mutex);
    }
    return get_item;
}


uint64_t devtab_get_uid(devtab_handle_t handle, devtab_node_t node) {
    void* uid_item;
    uid_item = sub_get_item((devtab_t*)handle, (devtab_item_t*)node, DEVTAB_uid);
    return uid_item ? *(uint64_t*)uid_item : 0;
}

uint16_t devtab_get_vid(devtab_handle_t handle, devtab_node_t node) {
    void* vid_item;
    vid_item = sub_get_item((devtab_t*)handle, (devtab_item_t*)node, DEVTAB_vid);
    return vid_item ? *(uint16_t*)vid_item : OTTER_PARAM_DEFMBSLAVE;
}

void* devtab_get_intf(devtab_handle_t handle, devtab_node_t node) {
    void* intf_item;
    intf_item = sub_get_item((devtab_t*)handle, (devtab_item_t*)node, DEVTAB_intf);
    return intf_item ? *(void**)intf_item : NULL;
}


void* devtab_get_rootctx(devtab_handle_t handle, devtab_node_t node) {
#if OTTER_FEATURE(SECURITY)
    void* ctx_item;
    ctx_item = sub_get_item((devtab_t*)handle, (devtab_item_t*)node, DEVTAB_rootctx);
    return ctx_item ? *(void**)ctx_item : NULL;
#else
    return NULL;
#endif
}


void* devtab_get_userctx(devtab_handle_t handle, devtab_node_t node) {
#if OTTER_FEATURE(SECURITY)
    void* ctx_item;
    ctx_item = sub_get_item((devtab_t*)handle, (devtab_item_t*)node, DEVTAB_userctx);
    return ctx_item ? *(void**)ctx_item : NULL;
#else
    return NULL;
#endif
}




uint64_t devtab_lookup_uid(devtab_handle_t handle, uint16_t vid) {
    devtab_node_t node;
    uint64_t uid = 0;
    node = devtab_select_vid(handle, vid);
    if (node != NULL) {
        uid = ((devtab_item_t*)node)->uid;
    }
    return uid;
}


uint16_t devtab_lookup_vid(devtab_handle_t handle, uint64_t uid) {
    return devtab_get_vid(handle, devtab_select(handle, uid));
}


void* devtab_lookup_intf(devtab_handle_t handle, uint64_t uid) {
    return devtab_get_intf(handle, devtab_select(handle, uid));
}


void* devtab_lookup_rootctx(devtab_handle_t handle, uint64_t uid) {
    return devtab_get_rootctx(handle, devtab_select(handle, uid));
}


void* devtab_lookup_userctx(devtab_handle_t handle, uint64_t uid) {
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
            output->intf    = NULL;
            output->rootctx = NULL;
            output->userctx = NULL;
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


static int sub_editop(devtab_t* table, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey, int operation) {
    devtab_item_t* item;
    devtab_vid_t* viditem;

    ///2. Insert a new cmd item,
    item = sub_searchop_uid(table, uid, operation);
    if (item == NULL) {
        return -3;
    }

    /// 3. add the vid index, if a VID is supplied, and link to the cell
    if (vid != 0) {
        viditem = sub_searchop_vid(table, vid, operation);
        if (viditem == NULL) {
            return -4;
        }
        viditem->cell = item;
    }

    return sub_edit_item(item, uid, vid, intfp, rootkey, userkey);
}


static int sub_setkey(void** ctx, void* key) {
#if OTTER_FEATURE(SECURITY)
    int rc = 0;
    
    if (key != NULL) {
        if (*ctx == NULL) {
            *ctx = calloc(1, sizeof(eax_ctx));
        }
        if (*ctx != NULL) {
            rc = (int)eax_init_and_key((io_t*)key, (eax_ctx*)*ctx);
//test_dumpbytes((const uint8_t*)key, 16, "New Key");
//fprintf(stderr, "test=%i, *ctx=%016llx\n", rc, (uint64_t)*ctx);
//test_dumpbytes((const uint8_t*)*ctx, sizeof(eax_ctx), "New CTX");
            if (rc != 0) {
                free(*ctx);
                rc = -2;
            }
        }
        else {
            rc = -1;
        }
    }
    else {
        if (*ctx != NULL) {
            free(*ctx);
        }
        *ctx = NULL;
    }
    
    return rc;
    
#else
    *ctx = NULL;
    return 0;
    
#endif
}


static int sub_edit_item(devtab_item_t* item, uint64_t uid, uint16_t vid, void* intfp, void* rootkey, void* userkey) {
    int rc;

    /// 4. Fill-up cell values.
    item->vid   = vid;
    item->intf  = intfp;
    
    rc = sub_setkey(&item->rootctx, rootkey);
    if (rc != 0) {
        return -1;
    }
    
    rc = sub_setkey(&item->userctx, userkey);
    if (rc != 0) {
        return -2;
    }
    
    return 0;
}


static int sub_cmp64(uint64_t a, uint64_t b) {
    return (a < b) - (a > b);
}

static int sub_cmp16(uint16_t a, uint16_t b) {
    return (a < b) - (a > b);
}



