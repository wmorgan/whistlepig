#ifndef WP_STRINGHASH_H_
#define WP_STRINGHASH_H_

// whistlepig string map
// (c) 2011 William Morgan. See COPYING for license terms.
//
// based on a heavily modified khash.h
//
// a stringmap is a bidirectional map from strings to int values. like termhash
// and stringpool, it uses a slightly funny API that never allocates memory,
// but instead operates on pointers to preallocated blocks of memory.
//
// uses a stringpool internally to do the int->string mapping. so if you're so
// you shouldn't have to interact with the stringpool directly; you can just
// use this object.
//
// like termhash and pool, has a slightly funny API that is designed to work on
// a pre-allocated chunk of memory rather than allocate any of its own.

#include <stdint.h>
#include "stringpool.h"
#include "error.h"

/* list of primes from khash.h:
  0ul,          3ul,          11ul,         23ul,         53ul,
  97ul,         193ul,        389ul,        769ul,        1543ul,
  3079ul,       6151ul,       12289ul,      24593ul,      49157ul,
  98317ul,      196613ul,     393241ul,     786433ul,     1572869ul,
  3145739ul,    6291469ul,    12582917ul,   25165843ul,   50331653ul,
  100663319ul,  201326611ul,  402653189ul,  805306457ul,  1610612741ul,
  3221225473ul, 4294967291ul
*/

#define INITIAL_N_BUCKETS_IDX 1

typedef struct stringmap {
  uint8_t n_buckets_idx;
  uint32_t n_buckets, size, n_occupied, upper_bound;
  uint32_t *flags;
  uint32_t *keys;
  stringpool* pool;
  uint8_t boundary[];
  // in memory at this point
  // ((n_buckets >> 4) + 1) uint32_t's for the flags
  // n_buckets uint32_t's for the keys
} stringmap;

// API methods

// public: write a new stringmap to memory
void stringmap_init(stringmap* h, stringpool* p);

// public: set up an existing stringmap in memory
void stringmap_setup(stringmap* h, stringpool* p);

// public: add a string. sets id to its id. dupes are fine; will just set the
// id correctly.
wp_error* stringmap_add(stringmap *h, const char* s, uint32_t* id) RAISES_ERROR;

// public: get the int value given a string. returns (uint32_t)-1 if not found.
uint32_t stringmap_string_to_int(stringmap* h, const char* s);

// public: get the string value given an int. returns corrupt data if the int
// is invalid.
const char* stringmap_int_to_string(stringmap* h, uint32_t i);

// public: returns the byte size of the stringmap
uint32_t stringmap_size(stringmap* h);

// public: returns the initial byte size for an empty stringmap
uint32_t stringmap_initial_size();

// public: returns the byte size for the next larger version of a stringmap
uint32_t stringmap_next_size(stringmap* h);

// public: does the stringmap need a size increase?
int stringmap_needs_bump(stringmap* h);

// public: increases the size of the stringmap
wp_error* stringmap_bump_size(stringmap *h) RAISES_ERROR;

#endif
