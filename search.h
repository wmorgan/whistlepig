#ifndef WP_SEARCH_H_
#define WP_SEARCH_H_

// whistlepig search code
// (c) 2011 William Morgan. See COPYING for license terms.
//
// what you need to know about search:
//  1. it runs on a per-segment basis; and
//  2. query objects maintain search state internally.
//
// to run a query on a segment, you need to use this call sequence:
//
// 1. wp_search_init_search_state
// 2. wp_search_run_query_on_segment (zero or more times)
// 3. wp_search_release_search_state
//
// because the query objects maintain state, you can repeat step 2 as much as
// you'd like to get more results without doing any duplicate work. if you
// don't do step 3, you'll leak memory.
//
// the corollary is that if you want to do multithreaded search across segments
// in parallel, you will have to clone the query for each segment to avoid
// sharing state.
//
// (right now the index does a serial search across segments, so cloning is not
// required.)

#include <stdint.h>

#include "defaults.h"
#include "segment.h"
#include "query.h"
#include "error.h"

// a match of a particular fielded phrase on a particular document
typedef struct doc_match {
  const char* field;
  const char* word;
  uint16_t num_positions;
  pos_t* positions;
} doc_match;

// a generic match on a document of a search stream
typedef struct search_result {
  docid_t doc_id;
  uint16_t num_doc_matches;
  doc_match* doc_matches;
} search_result;

struct wp_segment;
struct wp_query;
struct wp_error;

// API methods

// initialize the query search state for running on segment s. this must precede any call
// to wp_search_run_query_on_segment.
wp_error* wp_search_init_search_state(struct wp_query* q, struct wp_segment* s) RAISES_ERROR;

// release any query search state. this must follow any call to wp_search_run_query_on_segment.
wp_error* wp_search_release_search_state(struct wp_query* q) RAISES_ERROR;

// run a query on a segment, filling at most max_num_results slots in results.
// this is the main entry point into the actual search logic, and is called by
// index.c in various ways. this must be preceded by an init_search_state and
// followed by a release_search_state.
//
// if you get num_results > 0, you should call wp_search_result_free on each of the
// results when you're done with them.
wp_error* wp_search_run_query_on_segment(struct wp_query* q, struct wp_segment* s, uint32_t max_num_results, uint32_t* num_results, search_result* results) RAISES_ERROR;

// if you got non-zero num_results from wp_search_run_query_on_segment, call
// this on each result when you're done with it.
void wp_search_result_free(search_result* result);

#endif
