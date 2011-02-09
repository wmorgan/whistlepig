#ifndef WP_TERMHASH_H_
#define WP_TERMHASH_H_

// whistlepig term hash
// (c) 2011 William Morgan. See COPYING for license terms.
//
// based on a heavily modified khash.h
//
// a term, in this file, is a pair of ints. the intention is that these are the
// results of adding strings to the stringmap. the termhash is then a map from
// such pairs to ints.
//
// like stringmap and stringpool, it uses a slightly funny API that never
// allocates memory, but instead operates on pointers to preallocated blocks of
// memory.

#include <stdint.h>
#include "error.h"

typedef struct term {
  uint32_t field_s;
  uint32_t word_s;
} term;

#define INITIAL_N_BUCKETS_IDX 1

typedef struct termhash {
  uint8_t n_buckets_idx;
  uint32_t n_buckets, size, n_occupied, upper_bound;
  uint32_t *flags;
  term *keys;
  uint32_t *vals;
  uint8_t boundary[];
  // in memory at this point
  // ((n_buckets >> 4) + 1) uint32_t's for the flags
  // n_buckets terms for the keys
  // n_buckets uint32_t's for the vals (offsets into postings lists)
} termhash;

// API methods

// public: make a new termhash
void termhash_init(termhash* h);  // makes a new one

// public: set up an existing termhash
void termhash_setup(termhash* h); // inits one from disk

// private: khash-style getter: returns the slot id, if any, given a term key.
// you can then look this up within the vals array yourself. returns
// h->n_buckets if the term is not in the hash.
uint32_t termhash_get(termhash *h, term t); 

// public: get an int given a term. returns (uint32_t)-1 if the term is not in
// the hash.
uint32_t termhash_get_val(termhash* h, term t); // convenience

// private: khash-style setter: insert a term into the hash. see the code
// for details on what all the return values mean.
uint32_t termhash_put(termhash* h, term t, int *ret); // khash-style

// public: adds a term to the hash with the given value
wp_error* termhash_put_val(termhash* h, term t, uint32_t val) RAISES_ERROR; // convenience

// public: returns the byte size of the termhash
uint32_t termhash_size(termhash* h);

// public: returns the byte size for the next larger version of the termhash
uint32_t termhash_next_size(termhash* h);

// public: does the termhash need a size increase?
int termhash_needs_bump(termhash* h);

// public: increases the size of the termhash
wp_error* termhash_bump_size(termhash* h) RAISES_ERROR;

// public: returns the initial byte size for an empty termhash
uint32_t termhash_initial_size();

#endif
