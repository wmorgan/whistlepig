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

static char* strdup(const char* old) { // sigh... not in c99
  size_t len = strlen(old) + 1;
  char *new = malloc(len * sizeof(char));
  return memcpy(new, old, len);
}

wp_query* wp_query_clone(wp_query* other) {
  wp_query* ret = malloc(sizeof(wp_query));
  ret->type = other->type;
  ret->num_children = other->num_children;
  ret->search_data = NULL;

  if(other->field) ret->field = strdup(other->field);
  else ret->field = NULL;

  if(other->word) ret->word = strdup(other->word);
  else ret->word = NULL;

  ret->children = ret->next = ret->last = NULL; // set below
  for(wp_query* child = other->children; child != NULL; child = child->next) {
    wp_query* clone = wp_query_clone(child);
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

wp_query* wp_query_new_conjunction() {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_CONJ;
  return ret;
}

wp_query* wp_query_new_disjunction() {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_DISJ;
  return ret;
}

wp_query* wp_query_new_phrase() {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_PHRASE;
  return ret;
}

wp_query* wp_query_new_negation() {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_NEG;
  return ret;
}

wp_query* wp_query_new_empty() {
  wp_query* ret = wp_query_new();
  ret->type = WP_QUERY_EMPTY;
  return ret;
}

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

  return buf - orig_buf;
}

#define min(a, b) (a < b ? a : b)

int wp_query_to_s(wp_query* q, size_t n, char* buf) {
  int ret;
  char* orig_buf = buf;

  if(q->type == WP_QUERY_EMPTY) {
    buf[0] = '\0';
    ret = n;
  }
  else if(q->type == WP_QUERY_TERM) {
    size_t term_n = (size_t)snprintf(buf, n, "%s:\"%s\"", q->field, q->word);
    ret = min(term_n, n);
  }
  else if(q->type == WP_QUERY_LABEL) {
    size_t term_n = (size_t)snprintf(buf, n, "~%s", q->word);
    ret = min(term_n, n);
  }
  else {
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
