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

typedef struct postings_list_header {
  uint32_t count;
  uint32_t next_offset;
} postings_list_header;

typedef struct block_header {
  uint32_t max_docid;
  uint32_t next_offset;
  uint32_t block_start;
  uint8_t data[];
} block_header;

#define INITIAL_N_BUCKETS_IDX 1

typedef struct termhash {
  uint8_t n_buckets_idx;
  uint32_t n_buckets, size, n_occupied, upper_bound;
  uint8_t boundary[];
  // in memory at this point
  // ((n_buckets >> 4) + 1) uint32_t's for the flags
  // n_buckets terms for the keys
  // n_buckets postings_list_header for the vals
} termhash;

#define TERMHASH_FLAGS(h) ((uint32_t*)(h)->boundary)
#define TERMHASH_KEYS(h) ((term*)((uint32_t*)(h)->boundary + (((h)->n_buckets >> 4) + 1)))
#define TERMHASH_VALS(h) ((postings_list_header*)(TERMHASH_KEYS(h) + (h)->n_buckets))

// API methods

// public: make a new termhash
void termhash_init(termhash* h);  // makes a new one

// private: khash-style getter: returns the slot id, if any, given a term key.
// you can then look this up within the vals array yourself. returns
// h->n_buckets if the term is not in the hash.
uint32_t termhash_get(termhash *h, term t); 

// public: get an int given a term. returns (uint32_t)-1 if the term is not in
// the hash.
postings_list_header* termhash_get_val(termhash* h, term t); // convenience

// private: khash-style setter: insert a term into the hash. see the code
// for details on what all the return values mean.
uint32_t termhash_put(termhash* h, term t, int *ret); // khash-style

// public: adds a term to the hash with the given value
wp_error* termhash_put_val(termhash* h, term t, postings_list_header* val) RAISES_ERROR; // convenience

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
