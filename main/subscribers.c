//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "pktlist.h"

#include "cliopt.h"
#include "otter_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

///@todo STRATEGY
/// - each open/close operation opens a subscription that has a condwait or some such semaphore
/// - Top level binary search for id
/// - id in search table is connected to a linked list of subscriptions for that ID
/// - at first, just store signal information from post, not the packet data.


typedef void* subscr_t;


typedef struct subsnode {
    void*               buffers;    ///@todo buffers feature not yet implemented
    int                 sigmask;
    pthread_cond_t      cond;
    pthread_mutex_t     mutex;
    struct subsnode*    next;
    struct subsnode*    prev;
    void*               parent;
} subscr_node_t;


typedef struct {
    int                 alpid;
    subscr_node_t*      head;
} subscr_item_t;


typedef struct {
    subscr_item_t**   item;
    size_t            size;
    size_t            alloc;
} subscr_tab_t;





static int subscr_init(subscr_tab_t* table);
static void subscr_deinit(subscr_tab_t* table);
static void subscr_freenode(subscr_node_t* node);
static void subscr_freeitem(subscr_item_t* item);
static int subscr_add(subscr_tab_t* table, int alpid, subscr_node_t* item);
static const subscr_item_t* subscr_search(subscr_tab_t* table, int alpid);
static subscr_item_t* subscr_search_insert(subscr_tab_t* table, int alpid, bool do_insert);

static inline int local_cmp(int i1, int i2) {
    return (i1 < i2) - (i1 > i2);
}



int subscriber_init(void** handle) {
    int rc;
    subscr_tab_t* table;
    
    if (handle == NULL) {
        return -1;
    }
    
    table = malloc(sizeof(subscr_tab_t));
    if (table == NULL) {
        return -2;
    }
    
    rc = subscr_init(table);
    if (rc != 0) {
        free(table);
    }
    
    return rc;
}



void subscriber_deinit(void* handle) {
    subscr_tab_t* table = (subscr_tab_t*)handle;
    
    if (table != NULL) {
        subscr_deinit(table);
        free(table);
    }
}



subscr_t subscriber_new(void* handle, int alp_id, int sigmask, size_t max_frames, size_t max_payload) {
    subscr_tab_t*   table = (subscr_tab_t*)handle;
    subscr_item_t*  item;
    subscr_node_t*  oldhead;
    
    if (table == NULL) {
        goto subscriber_new_ERR;
    }
    
    item = subscr_search_insert(table, alp_id, true);
    if (item == NULL) {
        goto subscriber_new_ERR;
    }
    
    oldhead     = item->head;
    item->head  = malloc(sizeof(subscr_node_t));
    if (item->head == NULL) {
        goto subscriber_new_ERR1;
    }
    
    // Initialize Cond & Mutex
    if (pthread_mutex_init(&item->head->mutex, NULL) != 0) {
        goto subscriber_new_ERR2;
    }
    if (pthread_cond_init(&item->head->cond, NULL) != 0) {
        goto subscriber_new_ERR3;
    }
    
    // Initialize Buffers
    ///@todo buffers not presently used
    item->head->buffers = NULL;
    
    // Final Step: Link the list together
    // The Head node
    item->head->parent  = (void*)item;
    item->head->next    = oldhead;
    item->head->prev    = NULL;
    oldhead->prev       = item->head;
    
    return (subscr_t)item->head;
    
    // Error Handling
    subscriber_new_ERR3:    pthread_mutex_destroy(&item->head->mutex);
    subscriber_new_ERR2:    free(item->head);
    subscriber_new_ERR1:    item->head = oldhead;
    subscriber_new_ERR:     return NULL;
}



void subscriber_del(void* handle, subscr_t subscriber) {
    subscr_tab_t*   table   = (subscr_tab_t*)handle;
    subscr_node_t*  node    = (subscr_node_t*)subscriber;

    if (table != NULL) {
        subscr_freenode(node);
    
    }
}



int subscriber_open(subscr_t subscriber, int alp_id) {
    return 0;
}

int subscriber_close(subscr_t subscriber, int alp_id) {
    return 0;
}


int subscriber_wait(subscr_t subscriber, int alp_id) {
    return 0;
}


void subscriber_post(void* handle, int alp_id, uint8_t* payload, size_t size) {

}













static int subscr_init(subscr_tab_t* table) {
    if (table == NULL) {
        return -3;
    }

    table->item     = NULL;
    table->size     = 0;
    table->alloc    = 0;
    table->item     = malloc(OTTER_SUBSCR_CHUNK*sizeof(subscr_item_t*));
    
    if (table->item == NULL) {
        return -4;
    }
    
    table->alloc = OTTER_SUBSCR_CHUNK;
    return 0;
}



static void subscr_deinit(subscr_tab_t* table) {
    int i;
    
    if (table != NULL) {
        if (table->item != NULL) {
            i = (int)table->size;
            while (--i >= 0) {
                subscr_freeitem(table->item[i]);
            }
        }
        table->item     = NULL;
        table->size     = 0;
        table->alloc    = 0;
    }
}



static void subscr_freenode(subscr_node_t* node) {
     if (node != NULL) {
        // Destroy threading objects
        pthread_mutex_unlock(&node->mutex);
        pthread_mutex_destroy(&node->mutex);
        pthread_cond_destroy(&node->cond);
        
        ///@todo kill buffers
        
        // Relink node list and free this node
        if (node->next != NULL) {
            node->next->prev = node->prev;
        }
        if (node->prev != NULL) {
            node->prev->next = node->next;
        }
        else {
            ((subscr_item_t*)node->parent)->head = node->next;
        }
        free(node);
    }
}



static void subscr_freeitem(subscr_item_t* item) {
    if (item != NULL) {
        while (item->head != NULL) {
            subscr_freenode(item->head);
        }
        free(item);
    }
}



static int subscr_add(subscr_tab_t* table, int alpid, subscr_node_t* item) {
    subscr_item_t* newsubscr;
    
    if ((table == NULL) || (item == NULL)) {
        return -1;
    }
    
    ///1. Make sure there is room in the table
    if (table->alloc <= table->size) {
        subscr_item_t** newtable;
        size_t          newtable_alloc;
        
        newtable_alloc  = table->alloc + OTTER_SUBSCR_CHUNK;
        newtable        = realloc(table->item, newtable_alloc * sizeof(subscr_item_t*));
        if (newtable == NULL) {
            return -2;
        }
        
        table->item     = newtable;
        table->alloc    = newtable_alloc;
    }
    
    ///2. Insert a new cmd item, then fill the leftover items.
    ///   cmd_search_insert allocates and loads the name field.
    newsubscr = subscr_search_insert(table, alpid, true);
    
    if (newsubscr != NULL) {
        ///@todo go to end of list and add new subscriber
        return 0;
    }
    
    return -2;
}



static const subscr_item_t* subscr_search(subscr_tab_t* table, int alpid) {
    return subscr_search_insert(table, alpid, false);
}



static subscr_item_t* subscr_search_insert(subscr_tab_t* table, int alpid, bool do_insert) {
/// An important condition of using this function with insert is that table->alloc must
/// be greater than table->size.
///
/// Verify that cmdname is not a zero-length string, then search for it in the
/// list of available commands.
///
    subscr_item_t** head;
    subscr_item_t* output = NULL;
    int cci = 0;
    int csc = -1;
    int l, r;
    
    if (table == NULL) {
        return NULL;
    }

    l       = 0;
    r       = (int)table->size - 1;
    head    = table->item;
    
    while (r >= l) {
        cci = (l + r) >> 1;
        csc = local_cmp(head[cci]->alpid, alpid);
        
        switch (csc) {
            case -1: r = cci - 1;
                     break;
            
            case  1: l = cci + 1;
                     break;
            
            default: output = head[cci];
                     break;
        }
    }
    
    if (do_insert) {
        // If the alpid is unique, then it will not be found in the search
        // above, and output == NULL.
        if (output == NULL) {
            cci += (csc > 0);
            for (int i=(int)table->size; i>cci; i--) {
                head[i] = head[i-1];
            }
            output = malloc(sizeof(subscr_item_t));
            if (output != NULL) {
                head[cci] = output;
                output->alpid   = alpid;
                output->head    = NULL;
                table->size++;
            }
        }
    }
    
    return output;
}







