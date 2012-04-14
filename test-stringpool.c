#include <string.h>
#include "stringpool.h"
#include "error.h"
#include "test.h"

TEST(stringpool_initial_state) {
  stringpool* p = malloc(stringpool_initial_size());
  stringpool_init(p);

  ASSERT(!stringpool_needs_bump(p));

  free(p);
  return NO_ERROR;
}

TEST(stringpool_add_gives_unique_ids) {
  stringpool* p = malloc(stringpool_initial_size());
  stringpool_init(p);

  uint32_t ret1 = stringpool_add(p, "potato");
  ASSERT(ret1 > 0);

  uint32_t ret2 = stringpool_add(p, "monkey");
  ASSERT(ret2 > 0);

  ASSERT(ret1 != ret2);

  free(p);
  return NO_ERROR;
}

TEST(stringpool_add_gives_ids_that_lookup_returns) {
  stringpool* p = malloc(stringpool_initial_size());
  stringpool_init(p);

  uint32_t ret;
  char* s;

  ret = stringpool_add(p, "potato");
  s = stringpool_lookup(p, ret);
  ASSERT(!strcmp(s, "potato"));

  ret = stringpool_add(p, "monkey");
  s = stringpool_lookup(p, ret);
  ASSERT(!strcmp(s, "monkey"));

  free(p);
  return NO_ERROR;
}

TEST(stringpool_detects_out_of_room) {
  stringpool* p = malloc(stringpool_initial_size());
  stringpool_init(p);

  uint32_t ret;
  int times = stringpool_initial_size() / 6;
  for(int i = 0; i < times - 1; i++)  {
    ret = stringpool_add(p, "12345");
    ASSERT(ret != (uint32_t)-1);
  }

  ret = stringpool_add(p, "12345");
  ASSERT_EQUALS_UINT((uint32_t)-1, ret);

  return NO_ERROR;
}

