#ifndef WP_INDEX_H_
#define WP_INDEX_H_

// whistlepig index
// (c) 2011 William Morgan. See COPYING for license terms.
//
// the main public interaction point with whistlepig. controls everything
// except for the entry and query objects. it holds a collection of segments
// and essentially relays commands to the appropriate ones, creating new
// segments as needed.

#include <pthread.h>

#include "defaults.h"
#include "segment.h"
#include "error.h"
#include "entry.h"
#include "query.h"

#define WP_MAX_SEGMENTS 65534 // max value of wp_search_query->segment_idx - 2 because we need two special numbers

// in-memory representation of the index
typedef struct wp_index {
  const char* pathname_base;
  uint16_t num_segments;
  uint16_t sizeof_segments;
  uint64_t* docid_offsets;
  wp_segment* segments;
  uint8_t open;
  mmap_obj indexinfo;
} wp_index;

// serialized index info object
typedef struct index_info {
  uint32_t index_version;
  uint32_t num_segments;
  pthread_rwlock_t lock; // global r/w lock
} index_info;

// API methods

// public: returns non-zero if an index with base pathname pathname_base
// exists, zero otherwise
int wp_index_exists(const char* pathname_base);

// public: creates an index, raising an exception if it already exists
wp_error* wp_index_create(wp_index** index, const char* pathname_base) RAISES_ERROR;

// public: loads an existing index, raising an exception if it doesn't exist
wp_error* wp_index_load(wp_index** index, const char* pathname_base) RAISES_ERROR;

// public: releases an index
wp_error* wp_index_unload(wp_index* index) RAISES_ERROR;

// public: frees all memory. can be called after unload, or not. don't call
// anything on the index after calling this, though...
wp_error* wp_index_free(wp_index* index) RAISES_ERROR;

// public: returns the number of documents in the index.
wp_error* wp_index_num_docs(wp_index* index, uint64_t* num_docs) RAISES_ERROR;

// public: initializes a query for use on the index. must be called before
// run_query
wp_error* wp_index_setup_query(wp_index* index, wp_query* query) RAISES_ERROR;

// public: tears down a query from use on the index. must be called after
// run_query, or memory will leak.
wp_error* wp_index_teardown_query(wp_index* index, wp_query* query) RAISES_ERROR;

// public: runs a query on an index. must be called in between setup_query and
// teardown_query. can be called multiple times and the query will be resumed.
// when the number of documents returned is < num_results, then you're at the
// end!
wp_error* wp_index_run_query(wp_index* index, wp_query* query, uint32_t max_num_results, uint32_t* num_results, uint64_t* results) RAISES_ERROR;

// public: returns the number of results that match a query. note that this is
// roughly as expensive as just running the query competely, modulo some memory
// allocations here and there...
wp_error* wp_index_count_results(wp_index* index, wp_query* query, uint32_t* num_results) RAISES_ERROR;

// public: adds an entry to the index. sets doc_id to the new docid.
wp_error* wp_index_add_entry(wp_index* index, wp_entry* entry, uint64_t* doc_id) RAISES_ERROR;

// public: adds an label to a doc_id. throws an exception if the document
// doesn't exist. does nothing if the label has already been added to the
// document.
wp_error* wp_index_add_label(wp_index* index, const char* label, uint64_t doc_id);

// public: removes a label from a doc_id. throws an exception if the document
// doesn't exist. does nothing if the label has already been added to the
// document.
wp_error* wp_index_remove_label(wp_index* index, const char* label, uint64_t doc_id);

// dumps some index to the stream.
wp_error* wp_index_dumpinfo(wp_index* index, FILE* stream) RAISES_ERROR;

// public: deletes a document from disk.
wp_error* wp_index_delete(const char* path) RAISES_ERROR;

#endif
