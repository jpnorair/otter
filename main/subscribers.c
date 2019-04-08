//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#include "subscribers.h"

#include "pktlist.h"

#include "cliopt.h"
#include "otter_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>



///@todo STRATEGY
/// - each open/close operation opens a subscription that has a cond
/// - Top level binary search for id
/// - id in search table is connected to a linked list of subscriptions for that ID
/// - at first, just store signal information from post, not the packet data.


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
static subscr_item_t* subscr_add(subscr_tab_t* table, int alpid);
static const subscr_item_t* subscr_search(subscr_tab_t* table, int alpid);
static subscr_item_t* subscr_search_insert(subscr_tab_t* table, int alpid, bool do_insert);

static inline int local_cmp(int i1, int i2) {
    return (i1 < i2) - (i1 > i2);
}



int subscriber_init(subscr_handle_t* handle) {
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
    else {
        *handle = (subscr_handle_t)table;
    }
    
    return rc;
}



void subscriber_deinit(subscr_handle_t handle) {
    subscr_tab_t* table = (subscr_tab_t*)handle;
    
    if (table != NULL) {
        subscr_deinit(table);
        free(table);
    }
}



subscr_t subscriber_new(subscr_handle_t handle, int alp_id, size_t max_frames, size_t max_payload) {
    subscr_tab_t*   table = (subscr_tab_t*)handle;
    subscr_item_t*  item;
    subscr_node_t*  oldhead;
    
    if (table == NULL) {
        goto subscriber_new_ERR;
    }

    item = subscr_add(table, alp_id);
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

    // Set sigmask to zero (inactive)
    item->head->sigmask = 0;

    // Final Step: Link the list together
    // The Head node
    item->head->parent  = (void*)item;
    item->head->next    = oldhead;
    item->head->prev    = NULL;
    if (oldhead != NULL) {
        oldhead->prev   = item->head;
    }

    return (subscr_t)item->head;
    
    // Error Handling
    subscriber_new_ERR3:    pthread_mutex_destroy(&item->head->mutex);
    subscriber_new_ERR2:    free(item->head);
    subscriber_new_ERR1:    item->head = oldhead;
    subscriber_new_ERR:     return NULL;
}



void subscriber_del(subscr_handle_t handle, subscr_t subscriber) {
    subscr_tab_t*   table   = (subscr_tab_t*)handle;
    subscr_node_t*  node    = (subscr_node_t*)subscriber;

    if (table != NULL) {
        subscr_freenode(node);
    }
}



int subscriber_open(subscr_t subscriber, int sigmask) {
    subscr_node_t* node = (subscr_node_t*)subscriber;
    if (node == NULL) {
        return -1;
    }
    node->sigmask = sigmask;
    return 0;
}



int subscriber_close(subscr_t subscriber) {
    subscr_node_t* node = (subscr_node_t*)subscriber;
    if (node == NULL) {
        return -1;
    }
    node->sigmask = 0;
    return 0;
}


int subscriber_wait(subscr_t subscriber, int timeout_ms) {
    int rc;
    subscr_node_t* node = (subscr_node_t*)subscriber;
    
    if (node == NULL) {
        return -1;
    }
    
    if (timeout_ms <= 0) {
        rc = pthread_cond_wait(&node->cond, &node->mutex);
    }
    else {
        struct timespec abstime;
        struct timeval  tv;
        int timeout_s;
        
        timeout_s   = timeout_ms / 1000;
        timeout_ms %= 1000;
    
        gettimeofday(&tv, NULL);
        abstime.tv_sec  = tv.tv_sec + timeout_s;
        abstime.tv_nsec = (tv.tv_usec * 1000) + (timeout_ms * 1000000);
        if (abstime.tv_nsec >= 1000000000) {
            abstime.tv_nsec-= 1000000000;
            abstime.tv_sec += 1;
        }
    
        rc = pthread_cond_timedwait(&node->cond, &node->mutex, (const struct timespec*)&abstime);
    }
    pthread_mutex_unlock(&node->mutex);
    
    return rc;
}


void subscriber_post(subscr_handle_t handle, int alp_id, int signal, uint8_t* payload, size_t size) {
    subscr_tab_t*   table   = (subscr_tab_t*)handle;
    subscr_item_t*  item;
    subscr_node_t*  node;
    
    if (table != NULL) {
        item = subscr_search_insert(table, alp_id, false);
        if (item != NULL) {
            node = item->head;
            while (node != NULL) {
                if (node->sigmask & signal) {
                    pthread_cond_signal(&node->cond);
                }
                node = node->next;
            }
        }
    }
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



static subscr_item_t* subscr_add(subscr_tab_t* table, int alpid) {
    if (table == NULL) {
        return NULL;
    }
    
    ///1. Make sure there is room in the table
    if (table->alloc <= table->size) {
        subscr_item_t** newtable;
        size_t          newtable_alloc;
        newtable_alloc  = table->alloc + OTTER_SUBSCR_CHUNK;
        newtable        = realloc(table->item, newtable_alloc * sizeof(subscr_item_t*));
        if (newtable == NULL) {
            return NULL;
        }

        table->item     = newtable;
        table->alloc    = newtable_alloc;
    }
    
    ///2. Insert a new cmd item, then fill the leftover items.
    ///   cmd_search_insert allocates and loads the name field.
    return subscr_search_insert(table, alpid, true);
}



static const subscr_item_t* subscr_search(subscr_tab_t* table, int alpid) {
    return subscr_search_insert(table, alpid, false);
}



static subscr_item_t* subscr_search_insert(subscr_tab_t* table, int alpid, bool do_insert) {
/// An important condition of using this function with insert is that table->alloc must
/// be greater than table->size.
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
                     goto subscr_search_insert_ACTION;
        }
    }

    subscr_search_insert_ACTION:
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







