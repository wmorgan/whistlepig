#include "termhash.h"
#include "test.h"
#include "error.h"

TEST(termhash_initial_state) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  ASSERT_EQUALS_UINT(0, h->n_occupied);
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

  ASSERT(termhash_get_val(h, t1) == NULL);
  ASSERT(termhash_get_val(h, t2) == NULL);
  ASSERT(termhash_get_val(h, t3) == NULL);

  free(h);
  return NO_ERROR;
}

TEST(termhash_overwriting) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t1 = {5, 11};
  postings_list_header plh = { 1, 1234 };

  ASSERT(termhash_get_val(h, t1) == NULL);
  RELAY_ERROR(termhash_put_val(h, t1, &plh));

  postings_list_header* result = termhash_get_val(h, t1);
  ASSERT(result != NULL);
  ASSERT(result != &plh); // must make a copy
  ASSERT_EQUALS_UINT(1234, result->next_offset);

  postings_list_header plh2 = { 1, 2345 };
  RELAY_ERROR(termhash_put_val(h, t1, &plh2));
  ASSERT(termhash_get_val(h, t1)->next_offset == 2345);

  free(h);
  return NO_ERROR;
}

TEST(termhash_many_puts) { // try and force a resize
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t1 = {1, 0};
  postings_list_header plh = { 0, 0 };

  for(int i = 1; i < 100; i++) {
    t1.word_s = i;
    plh.next_offset = 1000 + i;
    RELAY_ERROR(termhash_put_val(h, t1, &plh));
    if(termhash_needs_bump(h)) {
      h = realloc(h, termhash_next_size(h));
      if(h == NULL) RAISE_SYSERROR("realloc");
      RELAY_ERROR(termhash_bump_size(h));
    }
  }

  t1.word_s = 55;
  postings_list_header* plh2 = termhash_get_val(h, t1);
  ASSERT_EQUALS_UINT(1055, plh2->next_offset);

  free(h);
  return NO_ERROR;
}

TEST(termhash_detects_out_of_room) {
  termhash* h = malloc(termhash_initial_size());
  termhash_init(h);

  term t = {1, 0};

  postings_list_header plh = { 0, 0 };
  for(int i = 0; i < 3; i++) {
    t.word_s = i;
    plh.next_offset = 1000 + i;
    RELAY_ERROR(termhash_put_val(h, t, &plh));
  }

  t.word_s = 999;
  plh.next_offset = 999;
  wp_error* e = termhash_put_val(h, t, &plh);
  ASSERT(e != NULL);
  wp_error_free(e);

  free(h);
  return NO_ERROR;
}
