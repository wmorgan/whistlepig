#ifndef WP_ENTRY_H_
#define WP_ENTRY_H_

// whistlepig entry
// (c) 2011 William Morgan. See COPYING for license terms.
//
// an entry is a document before being added to the index. it's nothig more
// than a map of (field,term) pairs to a (sorted) list of positions.  you can
// use this to incrementally build up a document in memory before adding it to
// the index.

#include "defaults.h"
#include "error.h"
#include "segment.h"
#include "khash.h"

typedef struct posarray {
  uint16_t size;
  uint16_t next;
  pos_t* data;
} posarray;

typedef struct fielded_term {
  char* field;
  char* term;
} fielded_term;

khint_t fielded_term_hash(fielded_term ft);
khint_t fielded_term_equals(fielded_term a, fielded_term b);

KHASH_INIT(entries, fielded_term, posarray*, 1, fielded_term_hash, fielded_term_equals);

typedef struct wp_entry {
  khash_t(entries)* entries;
  pos_t next_offset;
} wp_entry;

struct wp_segment;

// API methods

// public: make a new entry
wp_entry* wp_entry_new();

// public: return the number of tokens occurrences in the entry
uint32_t wp_entry_size(wp_entry* entry);

// public: add an individual token
wp_error* wp_entry_add_token(wp_entry* entry, const char* field, const char* term) RAISES_ERROR;

// public: add a string, which will be tokenized at spaces only
wp_error* wp_entry_add_string(wp_entry* entry, const char* field, const char* string) RAISES_ERROR;

// public: add a file, which will be tokenized at spaces only
wp_error* wp_entry_add_file(wp_entry* entry, const char* field, FILE* f) RAISES_ERROR;

// public: free an entry.
wp_error* wp_entry_free(wp_entry* entry) RAISES_ERROR;

// private: write to a segment
wp_error* wp_entry_write_to_segment(wp_entry* entry, struct wp_segment* seg, docid_t doc_id) RAISES_ERROR;

// private: calculate the size needed for a postings region
wp_error* wp_entry_sizeof_postings_region(wp_entry* entry, struct wp_segment* seg, uint32_t* size) RAISES_ERROR;

#endif
