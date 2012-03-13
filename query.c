#include "whistlepig.h"
#include "query.h"

static wp_query* wp_query_new() {
  wp_query* ret = malloc(sizeof(wp_query));
  ret->type = 0; // error
  ret->field = ret->word = NULL;
  ret->num_children = 0;
  ret->children = ret->next = ret->last = NULL;
  ret->search_data = NULL;

  return ret;
}

static const char* identity(const char* field, const char* word) {
  (void)field;
  if(word) return strdup(word);
  else return NULL;
}

wp_query* wp_query_clone(wp_query* other) {
  return wp_query_substitute(other, identity);
}

wp_query* wp_query_substitute(wp_query* other, const char *(*substituter)(const char* field, const char* word)) {
  wp_query* ret = malloc(sizeof(wp_query));
  ret->type = other->type;
  ret->num_children = other->num_children;
  ret->search_data = NULL;

  if(other->field) ret->field = strdup(other->field);
  else ret->field = NULL;

  if(other->field && other->word) ret->word = substituter(other->field, other->word);
  else ret->word = NULL;

  ret->children = ret->next = ret->last = NULL; // set below
  for(wp_query* child = other->children; child != NULL; child = child->next) {
    wp_query* clone = wp_query_substitute(child, substituter);
    if(ret->last == NULL) ret->children = ret->last = clone;
    else {
      ret->last->next = clone;
      ret->last = clone;
    }
  }

  return ret;
}

wp_query* wp_query_new_term(const char* field, const char* word) {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_TERM;
  ret->field = field;
  ret->word = word;
  return ret;
}

wp_query* wp_query_new_label(const char* label) {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_LABEL;
  ret->word = label;
  ret->field = NULL;
  return ret;
}

#define SIMPLE_QUERY_CONSTRUCTOR(name, type_name) \
  wp_query* wp_query_new_##name() { \
    wp_query* ret = wp_query_new(); \
    ret->type = type_name; \
    return ret; \
  }

SIMPLE_QUERY_CONSTRUCTOR(conjunction, WP_QUERY_CONJ);
SIMPLE_QUERY_CONSTRUCTOR(disjunction, WP_QUERY_DISJ);
SIMPLE_QUERY_CONSTRUCTOR(phrase, WP_QUERY_PHRASE);
SIMPLE_QUERY_CONSTRUCTOR(negation, WP_QUERY_NEG);
SIMPLE_QUERY_CONSTRUCTOR(empty, WP_QUERY_EMPTY);
SIMPLE_QUERY_CONSTRUCTOR(every, WP_QUERY_EVERY);

wp_query* wp_query_add(wp_query* a, wp_query* b) {
  if(a->type == WP_QUERY_EMPTY) {
    wp_query_free(a);
    return b;
  }
  else if(b->type == WP_QUERY_EMPTY) {
    wp_query_free(b);
    return a;
  }
  else {
    a->num_children++;
    if(a->last == NULL) a->children = a->last = b;
    else {
      a->last->next = b;
      a->last = b;
    }
    return a;
  }
}

void wp_query_free(wp_query* q) {
  if(q->field) free((void*)q->field);
  if(q->word) free((void*)q->word);
  while(q->children) {
    wp_query* b = q->children;
    q->children = q->children->next;
    wp_query_free(b);
  }
  free(q);
}

static int subquery_to_s(wp_query* q, size_t n, char* buf) {
  char* orig_buf = buf;

  for(wp_query* child = q->children; child != NULL; child = child->next) {
    if((n - (buf - orig_buf)) < 1) break; // can we add a space?
    buf += sprintf(buf, " ");
    buf += wp_query_to_s(child, n - (buf - orig_buf), buf);
  }

  return (int)(buf - orig_buf);
}

#define min(a, b) (a < b ? a : b)

size_t wp_query_to_s(wp_query* q, size_t n, char* buf) {
  size_t ret, term_n;
  char* orig_buf = buf;

  /* nodes without children */
  switch(q->type) {
  case WP_QUERY_TERM:
    term_n = (size_t)snprintf(buf, n, "%s:\"%s\"", q->field, q->word);
    ret = min(term_n, n);
    break;
  case WP_QUERY_LABEL:
    term_n = (size_t)snprintf(buf, n, "~%s", q->word);
    ret = min(term_n, n);
    break;
  case WP_QUERY_EMPTY:
    term_n = (size_t)snprintf(buf, n, "<EMPTY>");
    ret = min(term_n, n);
    break;
  case WP_QUERY_EVERY:
    term_n = (size_t)snprintf(buf, n, "<EVERY>");
    ret = min(term_n, n);
    break;

  /* nodes with children */
  default:
    switch(q->type) {
    case WP_QUERY_CONJ:
      if(n >= 4) { // "(AND"
        buf += snprintf(buf, n, "(AND");
        n -= 4;
      }
      break;
    case WP_QUERY_DISJ:
      if(n >= 3) { // "(OR"
        buf += snprintf(buf, n, "(OR");
        n -= 3;
      }
      break;
    case WP_QUERY_PHRASE:
      if(n >= 7) { // "(PHRASE"
        buf += snprintf(buf, n, "(PHRASE");
        n -= 7;
      }
      break;
    case WP_QUERY_NEG:
      if(n >= 4) {
        buf += snprintf(buf, n, "(NOT");
        n -= 4;
      }
      break;
    }

    int subq_size = subquery_to_s(q, n, buf);
    n -= subq_size;
    buf += subq_size;
    if(n >= 1) buf += sprintf(buf, ")");
    ret = buf - orig_buf;
  }

  return ret;
}

wp_query* wp_query_set_all_child_fields(wp_query* q, const char* field) {
  if(q->type == WP_QUERY_TERM) q->field = field;
  else for(wp_query* child = q->children; child != NULL; child = child->next) wp_query_set_all_child_fields(child, strdup(field));
  return q;
}
