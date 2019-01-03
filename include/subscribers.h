//
//  formatters.c
//  otter
//
//  Created by John Peter Norair on 19/7/17.
//  Copyright © 2017 JP Norair (Indigresso). All rights reserved.
//

#ifndef subscribers_h
#define subscribers_h

#include <stdio.h>
#include <stdint.h>


/// List of Subscriber Signals
/// Currently, just one (SIG_OK)
#define SUBSCR_SIG_OK       1



typedef void* subscr_t;
typedef void* subscr_handle_t;




/** @brief Initialize a subscriber system
  * @param handle   (subscr_handle_t*) Handle Pointer Result Parameter.
  * @retval int     0 on success, non-zero on error.
  * @sa subscriber_deinit
  * 
  * Call this function during process startup.  It will create a subscriber
  * object, and the resultant handle can be passed to threads in the process
  * that want to use subscriptions.
  */
int subscriber_init(subscr_handle_t* handle);


/** @brief De-Initialize a subscriber system
  * @param handle   (subscr_handle_t) Handle Pointer.
  * @retval None
  * @sa subscriber_init
  * 
  * Destroy all subscribers and free all memory.  Call on exit to process, as
  * conjugate to subscriber_init().
  */
void subscriber_deinit(subscr_handle_t handle);


/** @brief Create a new subscriber
  * @param handle       (subscr_handle_t) Handle Pointer.
  * @param alp_id       (int) ALP ID to subscribe to
  * @param max_frames   (size_t) Maximum number of frames to save
  * @param max_payload  (size_t) Maximum payload per saved frame
  * @retval subscr_t    New Subscriber object.  NULL on error.
  * @sa subscriber_del
  * 
  * The conjugate of this function is subscriber_del().  Creating a new 
  * subscriber is a heavier operation compared to open/close.  The idea is to 
  * create a new subscriber at the start of a thread, and then to open/close
  * the subscription, as needed, within the thread.
  */
subscr_t subscriber_new(subscr_handle_t handle, int alp_id, size_t max_frames, size_t max_payload);


/** @brief Delete a subscriber
  * @param handle       (subscr_handle_t) Handle Pointer.
  * @param subscriber   (subscr_t) Subscriber object to delete
  * @retval None
  * @sa subscriber_new
  * 
  * The conjugate of this function is subscriber_del().  Creating a new 
  * subscriber is a heavier operation compared to open/close.  The idea is to 
  * create a new subscriber at the start of a thread, and then to open/close
  * the subscription, as needed, within the thread.
  */
void subscriber_del(subscr_handle_t handle, subscr_t subscriber);



/** @brief Open/Activate a subscriber (subscriptions)
  * @param subscriber   (subscr_t) Subscriber object to open
  * @param sigmask      (int) Signal Mask.
  * @retval int         zero on success, non-zero on error.
  * @sa subscriber_close
  * 
  * The conjugate of this function is subscriber_close.  
  *
  * Before a subscriber can be opened, it must be created with subscriber_new.
  * Opening a subscriber requires a sigmask parameter.  Using sigmask=0 will
  * cause no subscriptions to get delivered.  Using other sigmasks will cause
  * subscriptions to get delivered when at least one of the signals identified
  * is masked.
  * 
  * Signal Values
  * SUBSCR_SIG_OK       ACK condition
  *
  */
int subscriber_open(subscr_t subscriber, int sigmask);



/** @brief Open/Activate a subscriber (subscriptions)
  * @param subscriber   (subscr_t) Subscriber object to close
  * @retval int         zero on success, non-zero on error.
  * @sa subscriber_open
  *
  * The conjugate of this function is subscriber_open.  
  *
  * Before a subscriber can be closed, it must be created with subscriber_new.
  * Closing a subscriber will prevent any subscriptions getting delivered or
  * saved.  However, it will not delete the subscriber.  It can be reopened 
  * with subscriber_open.  To delete the subscriber, use subscriber_del.
  */
int subscriber_close(subscr_t subscriber);


/** @brief Wait on next subscription to arrive
  * @param subscriber   (subscr_t) Subscriber object
  * @param timeout_ms   (int) timeout in milliseconds to stop waiting
  * @retval int         zero on success, non-zero on error.
  * @sa subscriber_post
  *
  * The value returned from subscriber_wait() is either:
  * Success (0), Parameter Error (-1), or a value returned by 
  * pthread_cond_timedwait (EINVAL, ETIMEDOUT)
  */
int subscriber_wait(subscr_t subscriber, int timeout_ms);


/** @brief Wait on next subscription to arrive
  * @param subscriber   (subscr_t) Subscriber object
  * @param timeout_ms   (int) timeout in milliseconds to stop waiting
  * @retval int         zero on success, non-zero on error.
  * @sa subscriber_post
  *
  */
void subscriber_post(subscr_handle_t handle, int alp_id, int signal, uint8_t* payload, size_t size);

#endif
