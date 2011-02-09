#ifndef WP_QUERY_PARSER_H_
#define WP_QUERY_PARSER_H_

// whistlepig query parser
// (c) 2011 William Morgan. See COPYING for license terms.
//
// most of the code, of course, is in the .lex and .y files

#include "query.h"
#include "error.h"

typedef struct {
  const char* input;
  const char* default_field;
  char* error;
  void* scanner;
  wp_query* result;
} query_parse_context;

// API methods

// public: parse a query from a string, attaching terms without fields to default_field
wp_error* wp_query_parse(const char* s, const char* default_field, wp_query** query) RAISES_ERROR;

#endif
