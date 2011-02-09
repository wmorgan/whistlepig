#include <string.h>
#include "stringmap.h"
#include "error.h"
#include "test.h"

static stringmap* setup() {
  stringpool* p = malloc(stringpool_initial_size());
  stringpool_init(p);
  stringmap* q = malloc(stringmap_initial_size());
  stringmap_init(q, p);
  return q;
}

TEST(stringmap_initial_state) {
  stringmap* q = setup();
  ASSERT(q->n_occupied == 0);
  ASSERT(!stringmap_needs_bump(q));

  free(q);
  return NO_ERROR;
}

TEST(stringmap_lookups_on_empty) {
  stringmap* q = setup();

  ASSERT(stringmap_string_to_int(q, "hot potato") == (uint32_t)-1);
  ASSERT(stringmap_int_to_string(q, 0) == NULL);
  ASSERT(stringmap_int_to_string(q, 1234) == NULL);

  free(q);
  return NO_ERROR;
}

TEST(stringmap_multiple_adds) {
  stringmap* q = setup();

  ASSERT(stringmap_string_to_int(q, "hot potato") == (uint32_t)-1);
  uint32_t x, y;
  RELAY_ERROR(stringmap_add(q, "hot potato", &x));
  ASSERT(x != (uint32_t)-1);
  RELAY_ERROR(stringmap_add(q, "hot potato", &y));
  ASSERT(y != (uint32_t)-1);
  ASSERT(x == y);

  free(q);
  return NO_ERROR;
}

TEST(stringmap_hashing_is_preserved) {
  stringmap* q = setup();

  uint32_t x, y;
  RELAY_ERROR(stringmap_add(q, "hello there", &x));
  ASSERT(x != (uint32_t)-1);
  const char* a = stringmap_int_to_string(q, x);
  ASSERT(strcmp(a, "hello there") == 0);

  RELAY_ERROR(stringmap_add(q, "how are you?", &y));
  const char* b = stringmap_int_to_string(q, y);
  ASSERT(strcmp(b, "how are you?") == 0);

  ASSERT(x != y);

  free(q);
  return NO_ERROR;
}

TEST(stringmap_detects_out_of_room) {
  stringmap* q = setup();

  uint32_t x, y, z, w;
  RELAY_ERROR(stringmap_add(q, "one", &x));
  RELAY_ERROR(stringmap_add(q, "two", &y));
  RELAY_ERROR(stringmap_add(q, "three", &z));

  wp_error* e = stringmap_add(q, "four", &w);
  ASSERT(e != NULL);
  wp_error_free(e);

  free(q);
  return NO_ERROR;
}
