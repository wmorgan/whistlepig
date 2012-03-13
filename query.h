#ifndef WP_QUERY_H_
#define WP_QUERY_H_

// whistlepig query
// (c) 2011 William Morgan. See COPYING for license terms.
//
// a query. typically built up by the parser, but you can also build it
// programmatically yourself if you like.
//
// note that queries contain segment-specific search state in them. see
// search.c for details.

#include <stdint.h>
#include <stdlib.h>
#include "segment.h"

#define WP_QUERY_TERM 1
#define WP_QUERY_CONJ 2
#define WP_QUERY_DISJ 3
#define WP_QUERY_PHRASE 4
#define WP_QUERY_NEG 5
#define WP_QUERY_LABEL 6
#define WP_QUERY_EMPTY 7
#define WP_QUERY_EVERY 8

// a node in the query tree
typedef struct wp_query {
  uint8_t type;
  const char* field;
  const char* word;

  uint16_t num_children;
  struct wp_query* children;
  struct wp_query* next;
  struct wp_query* last;

  uint16_t segment_idx; // used to continue queries across segments (see index.c)
  void* search_data; // whatever state we need for actually doing searches
} wp_query;

// API methods

// public: make a query node with a term
wp_query* wp_query_new_term(const char* field, const char* word);

// public: make a query node with a label
wp_query* wp_query_new_label(const char* label);

// public: make a query conjuction node
wp_query* wp_query_new_conjunction();

// public: make a query disjunction node
wp_query* wp_query_new_disjunction();

// public: make a query phrase node
wp_query* wp_query_new_phrase();

// public: make a query negation node
wp_query* wp_query_new_negation();

// public: make an empty query node.
wp_query* wp_query_new_empty();

// public: make an every-document query node.
wp_query* wp_query_new_every();

// public: deep clone of a query, dropping all search state.
wp_query* wp_query_clone(wp_query* other);

// public: build a new query by substituting words from the old query, dropping all search state
wp_query* wp_query_substitute(wp_query* other, const char *(*substituter)(const char* field, const char* word));

// public: add a query node as a child of another
wp_query* wp_query_add(wp_query* a, wp_query* b);

// private: set all children fields to a particular value
wp_query* wp_query_set_all_child_fields(wp_query* q, const char* field);

// public: free a query
void wp_query_free(wp_query* q);

// public: build a string representation of a query by writing at most n chars to buf
size_t wp_query_to_s(wp_query* q, size_t n, char* buf);

#endif
