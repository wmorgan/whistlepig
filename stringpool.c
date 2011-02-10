#include "whistlepig.h"

void stringpool_init(stringpool* p) {
  p->next = 1;
  p->size = INITIAL_POOL_SIZE;
}

uint32_t stringpool_size(stringpool* p) {
  return (uint32_t)sizeof(stringpool) + (p->size * (uint32_t)sizeof(char));
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

int stringpool_needs_bump(stringpool* p) {
  return (p->next >= (int)((float)p->size * 0.9) ? 1 : 0);
}

uint32_t stringpool_next_size(stringpool* p) {
  return (uint32_t)sizeof(stringpool) + (2 * (p->size == 0 ? 1 : p->size) * (uint32_t)sizeof(char));
}

uint32_t stringpool_initial_size() {
  return (uint32_t)sizeof(stringpool) + INITIAL_POOL_SIZE;
}

void stringpool_bump_size(stringpool* p) {
  p->size = stringpool_next_size(p);
}

char* stringpool_lookup(stringpool* p, uint32_t id) {
  if((id == 0) || (id >= p->next)) return NULL;
  return &p->pool[id];
}
