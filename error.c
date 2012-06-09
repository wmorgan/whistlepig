#include <stdlib.h>
#include "error.h"

wp_error* wp_error_new(const char* msg, const char* src, unsigned char type) {
  wp_error* ret = malloc(sizeof(wp_error));
  ret->msg = msg;
  ret->type = type;
  ret->size = 1;
  ret->srcs = malloc(sizeof(const char*));
  ret->srcs[0] = src;

  return ret;
}

wp_error* wp_error_chain(wp_error* e, const char* src) {
  e->size++;
  e->srcs = realloc(e->srcs, sizeof(const char*) * e->size);
  e->srcs[e->size - 1] = src;
  return e;
}

void wp_error_free(wp_error* e) {
  free(e->srcs);
  free(e);
}
