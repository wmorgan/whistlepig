#include "termhash.h"
#include "test.h"
#include "error.h"

TEST(termhash_initial_state) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  ASSERT(h->n_occupied == 0);
  //ASSERT(!termhash_getting_full(h));

  free(h);
  return NO_ERROR;
}

TEST(termhash_lookups_on_empty) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t1 = {0, 0};
  term t2 = {10, 20};
  term t3 = {123, 345};

  ASSERT(termhash_get_val(h, t1) == (uint32_t)-1);
  ASSERT(termhash_get_val(h, t2) == (uint32_t)-1);
  ASSERT(termhash_get_val(h, t3) == (uint32_t)-1);

  free(h);
  return NO_ERROR;
}

TEST(termhash_overwriting) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t1 = {5, 11};

  ASSERT(termhash_get_val(h, t1) == (uint32_t)-1);
  RELAY_ERROR(termhash_put_val(h, t1, 1234));
  ASSERT(termhash_get_val(h, t1) == 1234);

  RELAY_ERROR(termhash_put_val(h, t1, 2345));
  ASSERT(termhash_get_val(h, t1) == 2345);

  RELAY_ERROR(termhash_put_val(h, t1, 1));
  ASSERT(termhash_get_val(h, t1) == 1);

  free(h);
  return NO_ERROR;
}

TEST(termhash_many_puts) { // try and force a resize
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t1 = {1, 0};

  for(int i = 1; i < 100; i++) {
    t1.word_s = i;
    RELAY_ERROR(termhash_put_val(h, t1, 1000 + i));
    if(termhash_needs_bump(h)) {
      h = realloc(h, termhash_next_size(h));
      if(h == NULL) RAISE_SYSERROR("realloc");
      RELAY_ERROR(termhash_bump_size(h));
    }
  }

  t1.word_s = 55;
  uint32_t v = termhash_get_val(h, t1);
  ASSERT(v == 1055);

  free(h);
  return NO_ERROR;
}

TEST(termhash_detects_out_of_room) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t = {1, 0};

  for(int i = 0; i < 3; i++) {
    t.word_s = i;
    RELAY_ERROR(termhash_put_val(h, t, 100 + i));
  }

  t.word_s = 999;
  wp_error* e = termhash_put_val(h, t, 999);
  ASSERT(e != NULL);
  wp_error_free(e);

  free(h);
  return NO_ERROR;
}
