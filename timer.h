#ifndef WP_TIMER_H_
#define WP_TIMER_H_

// whistlepig main header file
// (c) 2011 William Morgan. See COPYING for license terms.
//
// just some timer macros

#include <sys/time.h>

#define TIMER(name) \
  struct timeval name##_startt, name##_endt; \
  long name##_elapsed;

#define START_TIMER(name) \
  TIMER(name) \
  gettimeofday(&name##_startt, NULL);

#define RESET_TIMER(name) gettimeofday(&name##_startt, NULL);

#define MARK_TIMER(name) \
  gettimeofday(&name##_endt, NULL); \
  name##_elapsed = ((name##_endt.tv_sec - name##_startt.tv_sec) * 1000) + ((name##_endt.tv_usec - name##_startt.tv_usec) / 1000);

#define TIMER_MS(name) name##_elapsed
#define TIMER_MS(name) name##_elapsed

#endif
