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

TEST(empty_queries) {
  wp_query* q;
  RELAY_ERROR(wp_query_parse("", "body", &q));
  ASSERT(q != NULL);

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
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("three five");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 3);

  RUN_QUERY("one five");
  ASSERT(num_results == 0);

  RUN_QUERY("three");
  ASSERT(num_results == 3);

  RUN_QUERY("one");
  ASSERT(num_results == 1);

  RUN_QUERY("one one one");
  ASSERT(num_results == 1);

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
  ASSERT(num_results == 2);
  ASSERT(results[0] == 2);
  ASSERT(results[1] == 1);

  RUN_QUERY("one OR two OR three");
  ASSERT(num_results == 3);
  ASSERT(results[0] == 3);
  ASSERT(results[1] == 2);
  ASSERT(results[2] == 1);

  RUN_QUERY("three OR two OR one");
  ASSERT(num_results == 3);
  ASSERT(results[0] == 3);
  ASSERT(results[1] == 2);
  ASSERT(results[2] == 1);

  RUN_QUERY("one OR one OR one");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("six OR one");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("six OR nine");
  ASSERT(num_results == 0);

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
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("\"one two\"");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("\"one two three\"");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("\"one two three four\"");
  ASSERT(num_results == 0);

  RUN_QUERY("\"three four five\"");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 3);

  RUN_QUERY("\"two three\"");
  ASSERT(num_results == 2);
  ASSERT(results[0] == 2);
  ASSERT(results[1] == 1);

  RUN_QUERY("\"two two\"");
  ASSERT(num_results == 0);

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
  ASSERT(num_results == 2);
  ASSERT(results[0] == 2);
  ASSERT(results[1] == 1);

  RUN_QUERY("two (three OR four)");
  ASSERT(num_results == 2);
  ASSERT(results[0] == 2);
  ASSERT(results[1] == 1);

  RUN_QUERY("two (three OR four OR five)");
  ASSERT(num_results == 2);
  ASSERT(results[0] == 2);
  ASSERT(results[1] == 1);

  RUN_QUERY("\"two three\" OR four");
  ASSERT(num_results == 3);

  RUN_QUERY("\"two three\" one");
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RUN_QUERY("three -one");
  ASSERT(num_results == 2);
  ASSERT(results[0] == 3);
  ASSERT(results[1] == 2);

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
  ASSERT(num_results == 1);
  ASSERT(results[0] == 3);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT(num_results == 1);
  ASSERT(results[0] == 2);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT(num_results == 0);

  RELAY_ERROR(wp_index_teardown_query(index, query));
  wp_query_free(query);

  // query matches one doc, one at a time
  RELAY_ERROR(wp_query_parse("one", "body", &query));
  RELAY_ERROR(wp_index_setup_query(index, query));
  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT(num_results == 1);
  ASSERT(results[0] == 1);

  RELAY_ERROR(wp_index_run_query(index, query, 1, &num_results, &results[0]));
  ASSERT(num_results == 0);

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
  ASSERT(num_results == 1);
  ASSERT(results[0] == 4);

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
  ASSERT(num_results == 0);

  RUN_QUERY("\"four five\"");
  ASSERT(num_results == 0);

  RUN_QUERY("four OR five");
  ASSERT(num_results == 0);

  RUN_QUERY("-hello");
  ASSERT(num_results == 0);

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
  ASSERT(num_results == 1);

  RUN_QUERY("body:there");
  ASSERT(num_results == 0);

  RUN_QUERY("there");
  ASSERT(num_results == 0);

  RUN_QUERY("subject:hello");
  ASSERT(num_results == 2);

  RUN_QUERY("body:hello");
  ASSERT(num_results == 0);

  RUN_QUERY("hello");
  ASSERT(num_results == 0);

  RUN_QUERY("subject:vacation body:harry");
  ASSERT(num_results == 1);

  RUN_QUERY("subject:vacation harry");
  ASSERT(num_results == 1);

  RUN_QUERY("subject:vacation -body:harry");
  ASSERT(num_results == 0);

  RUN_QUERY("subject:hello -body:harry");
  ASSERT(num_results == 1);

  RUN_QUERY("subject:hello body:\"i am going\"");
  ASSERT(num_results == 1);

  RUN_QUERY("-subject:hello body:\"i am going\"");
  ASSERT(num_results == 0);

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
  ASSERT(num_results == 1);

  RUN_QUERY("吞下");
  ASSERT(num_results == 1);

  RUN_QUERY("\"不 傷 身體\"");
  ASSERT(num_results == 1);

  RELAY_ERROR(shutdown(index));

  return NO_ERROR;
}
