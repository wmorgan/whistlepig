#include <string.h>
#include "defaults.h"
#include "stringpool.h"
#include "util.h"

void stringpool_init(stringpool* p) {
  p->next = 1;
  p->size = INITIAL_POOL_SIZE;
}

uint32_t stringpool_add(stringpool* p, const char* s) {
  uint32_t len = (uint32_t)strlen(s) + 1;

  if((p->next + len) >= p->size) {
    DEBUG("out of space in string pool for %s (len %d, next %d, size %d)", s, len, p->next, p->size);
    return (uint32_t)-1;
  }

  uint32_t ret = p->next;
  p->next += len;
  DEBUG("writing %d bytes to %p -- %p", len, &(p->pool[ret]), &(p->pool[ret]) + len);
  strncpy(&(p->pool[ret]), s, len);
  return ret;
}

char* stringpool_lookup(stringpool* p, uint32_t id) {
  if((id == 0) || (id >= p->next)) return NULL;
  return &p->pool[id];
}

uint32_t stringpool_initial_size() { return INITIAL_POOL_SIZE; }

uint32_t stringpool_size(stringpool* p) {
  return (uint32_t)sizeof(stringpool) + (p->size * (uint32_t)sizeof(char));
}

void stringpool_resize_to(stringpool* p, uint32_t new_size) {
  p->size = new_size - sizeof(stringpool);
}

uint32_t stringpool_next_size_for(stringpool* p, uint32_t additional) {
  uint32_t min_size = ((p->size + additional) * (uint32_t)sizeof(char));
  uint32_t next_size = nearest_upper_power_of_2(min_size);
  return next_size + (uint32_t)sizeof(stringpool);
}
