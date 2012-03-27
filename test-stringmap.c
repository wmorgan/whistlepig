#include <string.h>
#include "stringmap.h"
#include "error.h"
#include "test.h"

typedef struct map_and_pool {
  stringpool* pool;
  stringmap* map;
} map_and_pool;

static map_and_pool* setup() {
  map_and_pool* mp = malloc(sizeof(map_and_pool));
  mp->pool = malloc(stringpool_initial_size());
  stringpool_init(mp->pool);
  mp->map = malloc(stringmap_initial_size());
  stringmap_init(mp->map);
  return mp;
}

TEST(stringmap_initial_state) {
  map_and_pool* mp = setup();
  ASSERT(mp->map->n_occupied == 0);
  ASSERT(!stringmap_needs_bump(mp->map));

  free(mp);
  return NO_ERROR;
}

TEST(stringmap_lookups_on_empty) {
  map_and_pool* mp = setup();

  ASSERT(stringmap_string_to_int(mp->map, mp->pool, "hot potato") == (uint32_t)-1);
  ASSERT(stringmap_int_to_string(mp->map, mp->pool, 0) == NULL);
  ASSERT(stringmap_int_to_string(mp->map, mp->pool, 1234) == NULL);

  free(mp);
  return NO_ERROR;
}

TEST(stringmap_multiple_adds) {
  map_and_pool* mp = setup();

  ASSERT(stringmap_string_to_int(mp->map, mp->pool, "hot potato") == (uint32_t)-1);
  uint32_t x, y;
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "hot potato", &x));
  ASSERT(x != (uint32_t)-1);
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "hot potato", &y));
  ASSERT(y != (uint32_t)-1);
  ASSERT(x == y);

  free(mp);
  return NO_ERROR;
}

TEST(stringmap_hashing_is_preserved) {
  map_and_pool* mp = setup();

  uint32_t x, y;
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "hello there", &x));
  ASSERT(x != (uint32_t)-1);
  const char* a = stringmap_int_to_string(mp->map, mp->pool, x);
  ASSERT(strcmp(a, "hello there") == 0);

  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "how are you?", &y));
  const char* b = stringmap_int_to_string(mp->map, mp->pool, y);
  ASSERT(strcmp(b, "how are you?") == 0);

  ASSERT(x != y);

  free(mp);
  return NO_ERROR;
}

TEST(stringmap_detects_out_of_room) {
  map_and_pool* mp = setup();

  uint32_t x, y, z, w;
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "one", &x));
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "two", &y));
  RELAY_ERROR(stringmap_add(mp->map, mp->pool, "three", &z));

  wp_error* e = stringmap_add(mp->map, mp->pool, "four", &w);
  ASSERT(e != NULL);
  wp_error_free(e);

  free(mp);
  return NO_ERROR;
}
