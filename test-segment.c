#include "test.h"
#include "segment.h"
#include "tokenizer.lex.h"
#include "query.h"
#include "index.h"

#define SEGMENT_PATH "/tmp/segment-test"

wp_error* setup(wp_segment* segment) {
  RELAY_ERROR(wp_segment_delete(SEGMENT_PATH));
  RELAY_ERROR(wp_segment_create(segment, SEGMENT_PATH));
  return NO_ERROR;
}

#define ADD_DOC(word, pos) \
  positions[0] = pos; \
  RELAY_ERROR(wp_segment_ensure_fit(segment, postings_bytes, 0, &success)); \
  if(success != 1) RAISE_ERROR("couldn't ensure segment fit"); \
  RELAY_ERROR(wp_segment_add_posting(segment, "body", word, doc_id, 1, positions));

wp_error* add_docs(wp_segment* segment) {
  docid_t doc_id;
  pos_t positions[10];
  uint32_t postings_bytes;
  int success;

  RELAY_ERROR(wp_segment_sizeof_posarray(segment, 1, NULL, &postings_bytes));

  RELAY_ERROR(wp_segment_grab_docid(segment, &doc_id));
  ADD_DOC("one", 0);
  ADD_DOC("two", 1);
  ADD_DOC("three", 2);

  RELAY_ERROR(wp_segment_grab_docid(segment, &doc_id));
  ADD_DOC("two", 0);
  ADD_DOC("three", 1);
  ADD_DOC("four", 2);

  RELAY_ERROR(wp_segment_grab_docid(segment, &doc_id));
  ADD_DOC("three", 0);
  ADD_DOC("four", 1);
  ADD_DOC("five", 2);

  return NO_ERROR;
}

TEST(initial_state) {
  wp_segment segment;
  RELAY_ERROR(setup(&segment));

  segment_info* si = MMAP_OBJ(segment.seginfo, segment_info);
  ASSERT_EQUALS_UINT(0, si->num_docs);
  postings_region* pr = MMAP_OBJ(segment.postings, postings_region);
  ASSERT_EQUALS_UINT(0, pr->num_postings);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

TEST(adding_a_doc_increments_counts) {
  wp_segment segment;
  pos_t positions[10];
  docid_t doc_id;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(wp_segment_grab_docid(&segment, &doc_id));

  positions[0] = 0;
  RELAY_ERROR(wp_segment_add_posting(&segment, "body", "hello", doc_id, 1, positions));
  positions[0] = 1;
  RELAY_ERROR(wp_segment_add_posting(&segment, "body", "there", doc_id, 1, positions));

  segment_info* si = MMAP_OBJ(segment.seginfo, segment_info);
  ASSERT_EQUALS_UINT(1, si->num_docs);
  postings_region* pr = MMAP_OBJ(segment.postings, postings_region);
  ASSERT_EQUALS_UINT(2, pr->num_postings);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

#define RUN_QUERY(query) \
  RELAY_ERROR(wp_search_init_search_state(query, &segment)); \
  RELAY_ERROR(wp_search_run_query_on_segment(query, &segment, 10, &num_results, &results[0])); \
  RELAY_ERROR(wp_search_release_search_state(query));

TEST(simple_term_queries) {
  wp_segment segment;
  uint32_t num_results;
  search_result results[10];
  wp_query* query;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(add_docs(&segment));

  query = wp_query_new_term("body", "one");
  RUN_QUERY(query);

  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  query = wp_query_new_term("body", "two");
  RUN_QUERY(query);

  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(2, results[0].doc_id);
  ASSERT_EQUALS_UINT(1, results[1].doc_id);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

TEST(simple_conjunctive_queries) {
  wp_segment segment;
  uint32_t num_results;
  search_result results[10];
  wp_query* query;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(add_docs(&segment));

  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, wp_query_new_term("body", "two"));

  RUN_QUERY(query);

  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "four"));
  query = wp_query_add(query, wp_query_new_term("body", "two"));

  RUN_QUERY(query);

  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(2, results[0].doc_id);

  // <empty>
  query = wp_query_new_conjunction();
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

TEST(simple_phrasal_queries) {
  wp_segment segment;
  uint32_t num_results;
  search_result results[10];
  wp_query* query;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(add_docs(&segment));

  query = wp_query_new_phrase();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  query = wp_query_new_phrase();
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  query = wp_query_new_phrase();
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  query = wp_query_add(query, wp_query_new_term("body", "three"));
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(2, results[0].doc_id);
  ASSERT_EQUALS_UINT(1, results[1].doc_id);

  query = wp_query_new_phrase();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  query = wp_query_add(query, wp_query_new_term("body", "three"));
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

TEST(segment_conjuction_of_phrase_queries) {
  wp_segment segment;
  uint32_t num_results;
  search_result results[10];
  wp_query* query;
  wp_query* subquery;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(add_docs(&segment));

  // one "two three"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  // "two three" one
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, subquery);
  query = wp_query_add(query, wp_query_new_term("body", "one"));

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  // one "three two"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  // two "two three"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(2, results[0].doc_id);
  ASSERT_EQUALS_UINT(1, results[1].doc_id);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

TEST(negation_queries) {
  wp_segment segment;
  uint32_t num_results;
  search_result results[10];
  wp_query* query;
  wp_query* subquery;

  RELAY_ERROR(setup(&segment));
  RELAY_ERROR(add_docs(&segment));

  // one "two three"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  // "two three" one
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, subquery);
  query = wp_query_add(query, wp_query_new_term("body", "one"));

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(1, results[0].doc_id);

  // one "three two"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "one"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  // two "two three"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "two"));
  query = wp_query_add(query, subquery);

  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(2, results[0].doc_id);
  ASSERT_EQUALS_UINT(1, results[1].doc_id);

  // <empty>
  query = wp_query_new_conjunction();
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  // -one
  subquery = wp_query_new_term("body", "one");
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);

  // -two
  subquery = wp_query_new_term("body", "two");
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);

  // -three
  subquery = wp_query_new_term("body", "three");
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(0, num_results);

  // -potato
  subquery = wp_query_new_term("body", "potato");
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);
  ASSERT_EQUALS_UINT(1, results[2].doc_id);

  // -"one two"
  subquery = wp_query_new_conjunction();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "one"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "two"));
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);

  // -(AND one three)
  subquery = wp_query_new_conjunction();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "one"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);

  // -"one three"
  subquery = wp_query_new_phrase();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "one"));
  subquery = wp_query_add(subquery, wp_query_new_term("body", "three"));
  query = wp_query_new_negation();
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(3, num_results);

  // (AND -one three)
  subquery = wp_query_new_negation();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "one"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, subquery);
  query = wp_query_add(query, wp_query_new_term("body", "three"));
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);

  // (AND three -one)
  subquery = wp_query_new_negation();
  subquery = wp_query_add(subquery, wp_query_new_term("body", "one"));
  query = wp_query_new_conjunction();
  query = wp_query_add(query, wp_query_new_term("body", "three"));
  query = wp_query_add(query, subquery);
  RUN_QUERY(query);
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT(3, results[0].doc_id);
  ASSERT_EQUALS_UINT(2, results[1].doc_id);

  RELAY_ERROR(wp_segment_unload(&segment));
  return NO_ERROR;
}

