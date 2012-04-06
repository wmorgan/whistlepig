#ifndef WP_LOCK_H_
#define WP_LOCK_H_

// whistlepig locks
// (c) 2011 william morgan. see copying for license terms.

#include <pthread.h>

#include "error.h"

#define WP_LOCK_READLOCK 0
#define WP_LOCK_WRITELOCK 1

wp_error* wp_lock_setup(pthread_rwlock_t* lock) RAISES_ERROR;
wp_error* wp_lock_grab(pthread_rwlock_t* lock, int lock_type) RAISES_ERROR;
wp_error* wp_lock_release(pthread_rwlock_t* lock) RAISES_ERROR;

#endif
