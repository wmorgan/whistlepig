#include <stdio.h>
#include "whistlepig.h"
#include "query-parser.h"
#include "query-parser.tab.h"

int query_parser_parse(query_parse_context* c);
int query_parser_lex_init(void* scanner);
int query_parser_lex_destroy(void* scanner);
int query_parser_set_extra(void* extra, void* scanner);

void query_parser_error(YYLTYPE* locp, query_parse_context* context, const char* err) {
  context->error = malloc(1024 * sizeof(char));
  snprintf(context->error, 1024, "line %d: %s", locp->first_line, err);
}

extern int query_parser_debug;

wp_error* wp_query_parse(const char* s, const char* default_field, wp_query** query) {
  query_parse_context c;
  c.input = s;
  c.default_field = default_field;
  c.error = NULL;

  query_parser_lex_init(&c.scanner);
  query_parser_set_extra(&c, c.scanner);
  int ret = query_parser_parse(&c);
  query_parser_lex_destroy(c.scanner);

  if(ret != 0) RAISE_ERROR("parse error: %s", c.error);

  if(c.result == NULL) // empty query
    *query = wp_query_new_empty();
  else
    *query = c.result;

  return NO_ERROR;
}
