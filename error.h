#ifndef WP_ERROR_H_
#define WP_ERROR_H_

// whistlepig errors
// (c) 2011 William Morgan. See COPYING for license terms.
//
// a pseudo-backtrace calling convention that whistlepig uses extensively to
// systematically detect, relay and report errors. no fancy longjmp magic; just
// macros around return statements, basically.
//
// to write a new function that fits in this system:
//
// 1. have your function return a wp_error*.
// 2. mark your function as RAISES_ERROR in the declaration (or use
//    RAISING_STATIC for static functions that don't need a separate
//    declaration).
// 3. within the function, use RAISE_ERROR or RAISE_SYSERROR to raise a new
//    error and return.
// 4. within the function, use RELAY_ERROR to wrap all calls to functions that
//    return wp_error*.
// 5. return NO_ERROR if nothing happened.

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define WP_ERROR_TYPE_BASIC 1
#define WP_ERROR_TYPE_SYSTEM 2
#define WP_ERROR_TYPE_VERSION 3
#define WP_ERROR_TYPE_RESIZE 4

// pseudo-backtrace
typedef struct wp_error {
  unsigned char type;
  unsigned int size;
  char* msg;
  char** srcs;
  void* data;
} wp_error;

// data for resize signals
#define RESIZE_ERROR_POSTINGS_REGION 1
#define RESIZE_ERROR_STRINGPOOL 2
#define RESIZE_ERROR_STRINGMAP 3
#define RESIZE_ERROR_TERMHASH 4
#define RESIZE_ERROR_SEGMENT 5

typedef struct wp_resize_error_data {
  uint8_t what;
  uint32_t size;
} wp_resize_error_data;

// for functions
#define RAISES_ERROR __attribute__ ((warn_unused_result))
#define RAISING_STATIC(f) static wp_error* f RAISES_ERROR; static wp_error* f

// API methods

// private: make a new error object with a message and source line
wp_error* wp_error_new(char* msg, char* src, unsigned char type, void* data) RAISES_ERROR;
// private: add a source line to a pre-existing error
wp_error* wp_error_chain(wp_error* e, char* src) RAISES_ERROR;

// public: free an error, once handled
void wp_error_free(wp_error* e);

// private: internal mechanics for raising an error
#define RAISE_ERROR_OF_TYPE(type, data, fmt, ...) do { \
  char* msg = malloc(1024); \
  char* src = malloc(1024); \
  snprintf(msg, 1024, fmt, ## __VA_ARGS__); \
  snprintf(src, 1024, "%s (%s:%d)", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
  return wp_error_new(msg, src, type, data); \
} while(0)

// public: raise a basic error
#define RAISE_ERROR(fmt, ...) RAISE_ERROR_OF_TYPE(WP_ERROR_TYPE_BASIC, NULL, fmt, ## __VA_ARGS__)

// public: raise a version error
#define RAISE_VERSION_ERROR(fmt, ...) RAISE_ERROR_OF_TYPE(WP_ERROR_TYPE_VERSION, NULL, fmt, ## __VA_ARGS__)

// private: raise a resize "error" (more of a signal)
#define RAISE_RESIZE_ERROR(_what, _size) do { \
  wp_resize_error_data* _data = malloc(sizeof(wp_resize_error_data)); \
  _data->what = _what; \
  _data->size = _size; \
  RAISE_ERROR_OF_TYPE(WP_ERROR_TYPE_RESIZE, _data, "resize %u/%u", _what, _size); \
} while(0)

// public: raise a system error with strerror() automatically appended to the message
#define RAISE_SYSERROR(fmt, ...) RAISE_ERROR_OF_TYPE(WP_ERROR_TYPE_SYSTEM, NULL, fmt ": %s", ## __VA_ARGS__, strerror(errno))

// public: relay an error up the stack if the called function returns one.
#define RELAY_ERROR(e) do { \
  wp_error* __e = e; \
  if(__e != NULL) { \
    char* src = malloc(1024); \
    snprintf(src, 1024, "%s (%s:%d)", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    return wp_error_chain(__e, src); \
  } \
} while(0)

// public: print an error to stream
#define PRINT_ERROR(e, stream) do { \
  wp_error* __e = e; \
  fprintf(stream, "Error: %s\n", __e->msg); \
  for(unsigned int i = 0; i < e->size; i++) fprintf(stream, "  at %s\n", __e->srcs[i]); \
} while(0)

// public: print and exit if an error exists
#define DIE_IF_ERROR(e) do { \
  wp_error* __e = e; \
  if(__e != NULL) { \
    char* src = malloc(1024); \
    snprintf(src, 1024, "%s (%s:%d)", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    wp_error* err = wp_error_chain(__e, src); \
    PRINT_ERROR(err, stderr); \
    exit(-1); \
  } \
} while(0)

// return me if no error happens
#define NO_ERROR NULL

#endif
