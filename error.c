#include <stdlib.h>
#include "error.h"

wp_error* wp_error_new(char* msg, char* src, unsigned char type, void* data) {
  wp_error* ret = malloc(sizeof(wp_error));
  ret->msg = msg;
  ret->type = type;
  ret->size = 1;
  ret->srcs = malloc(sizeof(const char*));
  ret->srcs[0] = src;
  ret->data = data;

  return ret;
}

wp_error* wp_error_chain(wp_error* e, char* src) {
  e->size++;
  e->srcs = realloc(e->srcs, sizeof(char*) * e->size);
  e->srcs[e->size - 1] = src;
  return e;
}

void wp_error_free(wp_error* e) {
  free(e->msg);
  for(unsigned int i = 0; i < e->size; i++) free(e->srcs[i]);
  free(e->srcs);
  if(e->data) free(e->data);
  free(e);
}
