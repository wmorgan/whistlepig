#ifndef WP_STRINGPOOL_H_
#define WP_STRINGPOOL_H_

// whistlepig string pool
// (c) 2011 William Morgan. See COPYING for license terms.
//
// a string pool. adds strings to a big blob and returns an int which can be
// used to look them up later. in other words, an int->string mapping, where
// you provide the string and we'll give you an int.
//
// does no duplicate detection, if you add the same string twice, you will
// get two different ints and you will have wasted memory.
//
// this is used by stringmap to maintain a bidirectional string<->int mapping
// and is not really used directly.
//
// int 0 is a special case for the null string. passing in invalid ints (i.e.
// ints i didn't return) will result in garbage data.
//
// like termhash and stringmap, has a slightly funny API that is designed to
// work on a pre-allocated chunk of memory rather than allocate any of its own.

#include <stdint.h>

#define INITIAL_POOL_SIZE 2048

typedef struct stringpool {
  uint32_t size, next;
  char pool[];
} stringpool;

// API methods

void stringpool_init(stringpool* p);
uint32_t stringpool_add(stringpool* p, const char* s);
char* stringpool_lookup(stringpool* p, uint32_t id);
uint32_t stringpool_size(stringpool* p);
uint32_t stringpool_initial_size();
uint32_t stringpool_next_size_for(stringpool* p, uint32_t additional);
void stringpool_resize_to(stringpool* p, uint32_t new_size);

#endif
