#ifndef RARRAY_H_
#define RARRAY_H_

// a growable array list of things
#include "defaults.h"

#define RARRAY(type) wp_rarray_##type*
#define RARRAY_DECLARE(type) \
  typedef struct wp_rarray_##type {\
    uint32_t size; \
    uint32_t next; \
    type* data; \
  } wp_rarray_##type;

#define RARRAY_INIT(type, name) do { \
  RARRAY(type) _a = malloc(sizeof(wp_rarray_##type)); \
  _a->size = 1; \
  _a->next = 0; \
  _a->data = malloc(sizeof(type)); \
  (name) = _a; \
} while(0)

#define RARRAY_NELEM(name) (name)->next
#define RARRAY_ALL(name) (name)->data
#define RARRAY_GET(name, idx) (name)->data[idx]
#define RARRAY_FREE(type, name) do { \
  RARRAY(type) _a = (name); \
  free(_a->data); \
  free(_a); \
} while(0)

#define RARRAY_ADD(type, name, val) do { \
  RARRAY(type) _a = (name); \
  while(_a->next >= _a->size) { \
    _a->size *= 2; \
    _a->data = realloc(_a->data, _a->size * sizeof(type)); \
  } \
  _a->data[_a->next++] = (val); \
} while(0)

#endif
