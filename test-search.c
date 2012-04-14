#include "test.h"
#include "query.h"
#include "query-parser.h"
#include "index.h"

#define INDEX_PATH "/tmp/index-test"

RAISING_STATIC(add_string(wp_index* index, const char* string)) {
  uint64_t doc_id;
  wp_entry* entry = wp_entry_new();

  RELAY_ERROR(wp_entry_add_string(entry, "body", string));
  RELAY_ERROR(wp_index_add_entry(index, entry, &doc_id));
  RELAY_ERROR(wp_entry_free(entry));

  return NO_ERROR;
}

wp_error* setup(wp_index** index) {
  RELAY_ERROR(wp_index_delete(INDEX_PATH));
  RELAY_ERROR(wp_index_create(index, INDEX_PATH));

  RELAY_ERROR(add_string(*index, "one two three"));
  RELAY_ERROR(add_string(*index, "two three four"));
  RELAY_ERROR(add_string(*index, "three four five"));

  return NO_ERROR;
}

wp_error* shutdown(wp_index* index) {
  RELAY_ERROR(wp_index_free(index));
  RELAY_ERROR(wp_index_delete(INDEX_PATH));

  return NO_ERROR;
}

#define RUN_QUERY(q) \
  RELAY_ERROR(wp_query_parse(q, "body", &query)); \
  RELAY_ERROR(wp_index_setup_query(index, query)); \
  RELAY_ERROR(wp_index_run_query(index, query, 10, &num_results, &results[0])); \
  RELAY_ERROR(wp_index_teardown_query(index, query)); \
  wp_query_free(query); \

TEST(conjunctions) {
  wp_index* index;
  wp_query* query;
  uint64_t results[10];
  uint32_t num_results;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("one two");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("three five");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);

  RUN_QUERY("one five");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("three");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("one");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("one one one");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(disjunctions) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("one OR two");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);
  ASSERT_EQUALS_UINT64(1, results[1]);

  RUN_QUERY("one OR two OR three");
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);
  ASSERT_EQUALS_UINT64(1, results[2]);

  RUN_QUERY("three OR two OR one");
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);
  ASSERT_EQUALS_UINT64(1, results[2]);

  RUN_QUERY("one OR one OR one");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("six OR one");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("six OR nine");
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(phrases) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("\"one\"");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("\"one two\"");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("\"one two three\"");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("\"one two three four\"");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("\"three four five\"");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);

  RUN_QUERY("\"two three\"");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);
  ASSERT_EQUALS_UINT64(1, results[1]);

  RUN_QUERY("\"two two\"");
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(combinations) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("two (three OR one)");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);
  ASSERT_EQUALS_UINT64(1, results[1]);

  RUN_QUERY("two (three OR four)");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);
  ASSERT_EQUALS_UINT64(1, results[1]);

  RUN_QUERY("two (three OR four OR five)");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);
  ASSERT_EQUALS_UINT64(1, results[1]);

  RUN_QUERY("\"two three\" OR four");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("\"two three\" one");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RUN_QUERY("three -one");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(resumability) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  // query matches three docs, one at a time
  RELAY_ERROR(wp_query_parse("three", "body", &query));
  RELAY_ERROR(wp_index_setup_query(index, query));
  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(wp_index_teardown_query(index, query));
  wp_query_free(query);

  // query matches one doc, one at a time
  RELAY_ERROR(wp_query_parse("one", "body", &query));
  RELAY_ERROR(wp_index_setup_query(index, query));
  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(1, results[0]);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(wp_index_teardown_query(index, query));
  wp_query_free(query);

  return NO_ERROR;
}

// found a bug in the phrase matching that this captures
TEST(phrases_against_multiple_matches_in_doc) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  // add another doc
  RELAY_ERROR(add_string(index, "five four five six"));

  RUN_QUERY("\"four five six\"");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(4, results[0]);

  RELAY_ERROR(shutdown(index));

  return NO_ERROR;
}

TEST(queries_against_an_empty_index) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(wp_index_delete(INDEX_PATH));
  RELAY_ERROR(wp_index_create(&index, INDEX_PATH));

  RUN_QUERY("hello");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("\"four five\"");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("four OR five");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("-hello");
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(shutdown(index));

  return NO_ERROR;
}

TEST(fielded_queries) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;
  uint64_t doc_id;
  wp_entry* entry;

  RELAY_ERROR(wp_index_delete(INDEX_PATH));
  RELAY_ERROR(wp_index_create(&index, INDEX_PATH));

  entry = wp_entry_new();
  RELAY_ERROR(wp_entry_add_string(entry, "subject", "hello there bob"));
  RELAY_ERROR(wp_entry_add_string(entry, "from", "joe blow"));
  RELAY_ERROR(wp_entry_add_string(entry, "to", "bob bobson"));
  RELAY_ERROR(wp_entry_add_string(entry, "body", "hi bob how are you? i am fine. your friend, joe"));
  RELAY_ERROR(wp_index_add_entry(index, entry, &doc_id));
  RELAY_ERROR(wp_entry_free(entry));

  entry = wp_entry_new();
  RELAY_ERROR(wp_entry_add_string(entry, "subject", "hello i'm on vacation"));
  RELAY_ERROR(wp_entry_add_string(entry, "from", "joe blow"));
  RELAY_ERROR(wp_entry_add_string(entry, "to", "harry harrison"));
  RELAY_ERROR(wp_entry_add_string(entry, "body", "hi harry i am going to vacation now bye"));
  RELAY_ERROR(wp_index_add_entry(index, entry, &doc_id));
  RELAY_ERROR(wp_entry_free(entry));

  RUN_QUERY("subject:there");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("body:there");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("there");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("subject:hello");
  ASSERT_EQUALS_UINT(2, num_results);

  RUN_QUERY("body:hello");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("hello");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("subject:vacation body:harry");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("subject:vacation harry");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("subject:vacation -body:harry");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("subject:hello -body:harry");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("subject:hello body:\"i am going\"");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("-subject:hello body:\"i am going\"");
  ASSERT_EQUALS_UINT(0, num_results);

  return NO_ERROR;
}

TEST(utf8_chars) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  RELAY_ERROR(add_string(index, "hello 我 能 吞下 玻璃 而 不 傷 身體 。")); // pre-tokenized for your pleasure

  RUN_QUERY("我");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("吞下");
  ASSERT_EQUALS_UINT(1, num_results);

  RUN_QUERY("\"不 傷 身體\"");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(shutdown(index));

  return NO_ERROR;
}

TEST(every_selector) {
  wp_index* index;
  uint64_t results[10];
  uint32_t num_results;
  wp_query* query;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("*");
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);
  ASSERT_EQUALS_UINT64(1, results[2]);

  RUN_QUERY("-*");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("* OR four");
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);
  ASSERT_EQUALS_UINT64(1, results[2]);

  RUN_QUERY("* OR asdfasefs");
  ASSERT_EQUALS_UINT(3, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);
  ASSERT_EQUALS_UINT64(1, results[2]);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}
