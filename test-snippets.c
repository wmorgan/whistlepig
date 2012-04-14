#include "test.h"
#include "whistlepig.h"

const char* text = "sing to me of the man, muse, the man of twists and turns \
driven time and again off course, once he had plundered \
the hallowed heights of troy. \
many cities of men he saw and learned their minds, \
many pains he suffered, heartsick on the open sea, \
fighting to save his life and bring his comrades home. \
but he could not save them from disaster, hard as he strove — \
the recklessness of their own ways destroyed them all, \
the blind fools, they devoured the cattle of the sun \
and the sungod blotted out the day of their return. \
launch out on his story, muse, daughter of zeus, \
start from where you will — sing for our time too.";

TEST(empty_queries) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(0, num_results);

  return NO_ERROR;
}

TEST(terms) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("sing", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(2, num_results);

  ASSERT_EQUALS_UINT(0, start_offsets[0]);
  ASSERT_EQUALS_UINT(4, end_offsets[0]);

  return NO_ERROR;
}

TEST(conjuctions) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("sing muse", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(4, num_results);

  ASSERT_EQUALS_UINT(0, start_offsets[0]);
  ASSERT_EQUALS_UINT(4, end_offsets[0]);

  ASSERT_EQUALS_UINT(23, start_offsets[1]);
  ASSERT_EQUALS_UINT(27, end_offsets[1]);

  ASSERT_EQUALS_UINT(549, start_offsets[2]);
  ASSERT_EQUALS_UINT(553, end_offsets[2]);

  ASSERT_EQUALS_UINT(603, start_offsets[3]);
  ASSERT_EQUALS_UINT(607, end_offsets[3]);

  return NO_ERROR;
}

TEST(disjunctions) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("sing OR muse", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(4, num_results);

  // same as above
  ASSERT_EQUALS_UINT(0, start_offsets[0]);
  ASSERT_EQUALS_UINT(4, end_offsets[0]);

  ASSERT_EQUALS_UINT(23, start_offsets[1]);
  ASSERT_EQUALS_UINT(27, end_offsets[1]);

  ASSERT_EQUALS_UINT(549, start_offsets[2]);
  ASSERT_EQUALS_UINT(553, end_offsets[2]);

  ASSERT_EQUALS_UINT(603, start_offsets[3]);
  ASSERT_EQUALS_UINT(607, end_offsets[3]);

  return NO_ERROR;
}

TEST(phrases) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("\"sing to me of the man\"", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(1, num_results);

  ASSERT_EQUALS_UINT(0, start_offsets[0]);
  ASSERT_EQUALS_UINT(21, end_offsets[0]);

  return NO_ERROR;
}

TEST(negations) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("-sing", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));
  ASSERT_EQUALS_UINT(0, num_results);

  return NO_ERROR;
}

TEST(conjunctions_with_negations) {
  wp_query* q;
  uint32_t num_results;
  pos_t start_offsets[10];
  pos_t end_offsets[10];

  RELAY_ERROR(wp_query_parse("devoured -sing", "body", &q));
  RELAY_ERROR(wp_snippetize_string(q, "body", text, 10, &num_results, start_offsets, end_offsets));

  //printf("got %d results\n", num_results);
  ASSERT_EQUALS_UINT(1, num_results);

  //printf("got %d--%d\n", start_offsets[0], end_offsets[0]);
  ASSERT_EQUALS_UINT(441, start_offsets[0]);
  ASSERT_EQUALS_UINT(449, end_offsets[0]);

  return NO_ERROR;
}
