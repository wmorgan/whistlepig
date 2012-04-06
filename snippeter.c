#include "whistlepig.h"
#include "tokenizer.lex.h"

typedef struct pword {
  const char* token;
  pos_t start;
  pos_t end;
} pword;
RARRAY_DECLARE(pword);

RAISING_STATIC(is_match(wp_query* query, const char* field, RARRAY(pword) words, uint32_t start, uint32_t* end, int* found)) {
  wp_query* child;

  *found = 0;
  switch(query->type) {
  // for these four guys, we never match on snippets
  case WP_QUERY_LABEL:
  case WP_QUERY_EMPTY:
  case WP_QUERY_NEG:
  case WP_QUERY_EVERY:
    break;

  // terms match only if it's an exact match
  case WP_QUERY_TERM:
    DEBUG("term: comparing %s:%s to %s:%s", query->field, query->word, field, RARRAY_GET(words, start).token);
    if(!strcmp(field, query->field) && !strcmp(query->word, RARRAY_GET(words, start).token)) {
      *end = start;
      *found = 1;
    }
    break;

  // for conjunctions AND disjunctions, we match if any of the subclauses
  // match. this makes sense for conjunctions because the query "bob AND joe"
  // should produce a snippet for occurrences of either bob or joe, even if
  // the document semantics are different.
  case WP_QUERY_CONJ:
  case WP_QUERY_DISJ:
    child = query->children;
    while(child != NULL) {
      RELAY_ERROR(is_match(child, field, words, start, end, found));
      if(*found) break;
      child = child->next;
    }
    break;
  // phrases we have to do the hard way
  case WP_QUERY_PHRASE:
    child = query->children;
    if(strcmp(child->field, field)) break; // just look at the first one
    while(child != NULL) {
      DEBUG("phrase: comparing %s:%s to %s:%s", child->field, child->word, field, RARRAY_GET(words, start).token);
      if(strcmp(child->word, RARRAY_GET(words, start).token)) break;
      start++;
      child = child->next;
    }
    if(child == NULL) { // made it all the way through!
      *end = start - 1;
      *found = 1;
    }
    break;
  }

  return NO_ERROR;
}

RAISING_STATIC(snippetize_query(wp_query* query, const char* field, RARRAY(pword) words, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets)) {
  uint32_t idx = 0;
  *num_results = 0;

  while((*num_results < max_num_results) && (idx < RARRAY_NELEM(words))) {
    uint32_t final_idx;
    int found;
    RELAY_ERROR(is_match(query, field, words, idx, &final_idx, &found));
    if(found) {
      start_offsets[*num_results] = RARRAY_GET(words, idx).start;
      end_offsets[*num_results] = RARRAY_GET(words, final_idx).end;
      (*num_results)++;
      idx = final_idx + 1;
    }
    else idx++;
  }

  return NO_ERROR;
}

RAISING_STATIC(snippetize_from_lexer(wp_query* query, lexinfo* charpos, yyscan_t* scanner, const char* field, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets)) {
  RARRAY(pword) words;

  RARRAY_INIT(pword, words);
  while(yylex(*scanner) != TOK_DONE) {
    pword pw = { strdup(yyget_text(*scanner)), charpos->start, charpos->end };
    RARRAY_ADD(pword, words, pw);
  }

  RELAY_ERROR(snippetize_query(query, field, words, max_num_results, num_results, start_offsets, end_offsets));

  return NO_ERROR;
}

// tokenizes and adds everything under a single field
wp_error* wp_snippetize_string(wp_query* query, const char* field, const char* string, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);
  YY_BUFFER_STATE state = yy_scan_string(string, scanner);
  RELAY_ERROR(snippetize_from_lexer(query, &charpos, &scanner, field, max_num_results, num_results, start_offsets, end_offsets));
  yy_delete_buffer(state, scanner);
  yylex_destroy(scanner);

  return NO_ERROR;
}

// tokenizes and adds everything from a file under a single field
wp_error* wp_snippetize_file(wp_query* query, const char* field, FILE* f, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);
  yyset_in(f, scanner);
  RELAY_ERROR(snippetize_from_lexer(query, &charpos, &scanner, field, max_num_results, num_results, start_offsets, end_offsets));
  yylex_destroy(scanner);

  return NO_ERROR;
}

