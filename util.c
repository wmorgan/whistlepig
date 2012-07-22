#include <string.h>
#include <stdlib.h>
#include "util.h"

// from http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
uint32_t nearest_upper_power_of_2(uint32_t v) {
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;

  return v;
}

// not defined in c99 :(
char* strdup(const char* old) {
  size_t len = strlen(old) + 1;
  char *new = malloc(len * sizeof(char));
  return memcpy(new, old, len);
}

