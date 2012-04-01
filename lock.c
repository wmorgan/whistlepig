#include <unistd.h>
#include <pthread.h>
#include "whistlepig.h"

wp_error* wp_lock_setup(pthread_rwlock_t* lock) {
  pthread_rwlockattr_t attr;
  if(pthread_rwlockattr_init(&attr) != 0) RAISE_SYSERROR("cannot initialize pthreads rwlock attr");
  if(pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) RAISE_SYSERROR("cannot set pthreads rwlockattr to PTHREAD_PROCESS_SHARED");
  if(pthread_rwlock_init(lock, &attr) != 0) RAISE_SYSERROR("cannot initialize pthreads rwlock");
  if(pthread_rwlockattr_destroy(&attr) != 0) RAISE_SYSERROR("cannot destroy pthreads rwlock attr");

  return NO_ERROR;
}

/*

alernative implementation that uses rdlock. doesn't allow us to detect
and break stale locks, but gives us a good sense of what the timing should
be like.

wp_error* wp_segment_grab_lock2(wp_segment* seg, int lock_type) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  const char* lock_name = (lock_type == WP_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock for segment %p", lock_name, seg);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  int ret = 0;
  switch(lock_type) {
    case WP_LOCK_READLOCK: ret = pthread_rwlock_rdlock(&si->lock); break;
    case WP_LOCK_WRITELOCK: ret = pthread_rwlock_wrlock(&si->lock); break;
  }

  if(ret != 0) RAISE_SYSERROR("grabbing %slock", lock_name);

  clock_gettime(CLOCK_MONOTONIC, &end);


  uint64_t diff_in_ns = ((end.tv_sec * 1000000000) + end.tv_nsec) -
             ((start.tv_sec * 1000000000) + start.tv_nsec);

  uint32_t total_delay_ms = diff_in_ns / 1000000;
  if(total_delay_ms > 0) printf("XXX acquired %slock for segment %p after %ums\n", lock_name, seg, total_delay_ms);

  return NO_ERROR;
}
*/

// we will wait this many milliseconds before assuming the lock
// is stale and breaking it.
#define LOCK_STALE_TIME_MS 2500

/* here's the best implementation i can find, empirically, of being
   able to grab pthread read and write locks, while still being able
   to detect stale locks and repair them.

   it involves a busyloop, which is lame.
*/
wp_error* wp_lock_grab(pthread_rwlock_t* lock, int lock_type) {
  const char* lock_name = (lock_type == WP_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock at %p", lock_name, lock);

  unsigned int delay_ms = 1;
  uint32_t total_delay_ms = 0;

  while(1) {
    int ret = 0;

    switch(lock_type) {
      case WP_LOCK_READLOCK: ret = pthread_rwlock_tryrdlock(lock); break;
      case WP_LOCK_WRITELOCK: ret = pthread_rwlock_trywrlock(lock); break;
      default: RAISE_ERROR("invalid lock type");
    }

    if(ret == 0) break; // acquired!
    // we get EAGAINs here if the writer died before closing the lock.
    if((ret != EBUSY) && (ret != EAGAIN)) RAISE_SYSERROR("acquiring %slock", lock_name);

    if(total_delay_ms >= LOCK_STALE_TIME_MS) {
      //RAISE_ERROR("timeout acquiring %slock: %ums", lock_name, total_delay_ms);
      DEBUG("assuming lock is stale and breaking it!");
      RELAY_ERROR(wp_lock_setup(lock));
    }
    if(delay_ms > 1000) sleep(delay_ms / 1000);
    usleep(1000 * (delay_ms % 1000));
    total_delay_ms += delay_ms;
  }

  if(total_delay_ms > 0) DEBUG(":( acquired %slock for after %ums\n", lock_name, total_delay_ms);
  return NO_ERROR;
}

/* an alternative implementation that uses the _timed pthread operations.
   although this should be the best version, i had many problems with it.  the
   timeout didn't seem to ever trigger. i would also see an EINVAL whenever a
   writer had a readlock. in the case of a stale lock, i would just get EINVALS
   forever rather than a proper ETIMEDOUT.

   since using this would require implementing my own stale lock detection
   anyways, so i'm just going to use the simpler version above instead.
*/
/*
wp_error* wp_segment_grab_lock3(wp_segment* seg, int lock_type) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  const char* lock_name = (lock_type == WP_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock for segment %p", lock_name, seg);

  struct timespec timeout;
  timeout.tv_sec = 3;//LOCK_STALE_TIME_MS / 1000;
  timeout.tv_nsec = 0;//(LOCK_STALE_TIME_MS % 1000) * 1000000;

  struct timeval startt, endt;
  gettimeofday(&startt, NULL);

  int acquired = 0;
  while(!acquired) {
    int ret = 0;

    switch(lock_type) {
      case WP_LOCK_READLOCK: ret = pthread_rwlock_timedrdlock(&si->lock, &timeout); break;
      case WP_LOCK_WRITELOCK: ret = pthread_rwlock_timedwrlock(&si->lock, &timeout); break;
      default: RAISE_ERROR("invalid lock type");
    }

    switch(ret) {
      case 0: acquired = 1; break;
      case ETIMEDOUT:
        DEBUG("assuming lock is stale and breaking it!");
        RELAY_ERROR(setup_lock(&si->lock));
        break;
      case EAGAIN:
        // despite the documentation, this seems to happen every time we request a readlock and
        // the lock is already held by the writer. so we will just busyloop here. this happens
        // fairly frequently, so this is lame.
        usleep(1000);
        break;
      default:
        RAISE_SYSERROR("acquiring %slock", lock_name);
    }
  }
  gettimeofday(&endt, NULL);
  long elapsed = ((endt.tv_sec - startt.tv_sec) * 1000) + ((endt.tv_usec - startt.tv_usec) / 1000);

  if(elapsed > 0) printf(":( acquired %slock for segment %p after %ldms\n", lock_name, seg, elapsed);
  return NO_ERROR;
}
*/

wp_error* wp_lock_release(pthread_rwlock_t* lock) {
  if(pthread_rwlock_unlock(lock) != 0) RAISE_SYSERROR("releasing lock");
  return NO_ERROR;
}
