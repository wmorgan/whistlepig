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

  RELAY_ERROR(add_string(*index, "one two three")); // 1
  RELAY_ERROR(add_string(*index, "two three four")); // 2
  RELAY_ERROR(add_string(*index, "three four five")); // 3

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

TEST(added_labels_appear_in_search) {
  wp_index* index;
  wp_query* query;
  uint64_t results[10];
  uint32_t num_results;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("three");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 1));
  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 1));
  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 2));
  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(2, num_results);

  RUN_QUERY("four ~bob");
  ASSERT_EQUALS_UINT(1, num_results);

  // now add some in reverse docid order
  RELAY_ERROR(wp_index_add_label(index, "potato", 3));
  RUN_QUERY("~potato");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(wp_index_add_label(index, "potato", 2));
  RELAY_ERROR(wp_index_add_label(index, "potato", 1));
  RUN_QUERY("~potato");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("~potato ~bob");
  ASSERT_EQUALS_UINT(2, num_results);

  RUN_QUERY("~potato -~bob");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(removing_labels_disappear_from_search) {
  wp_index* index;
  wp_query* query;
  uint64_t results[10];
  uint32_t num_results;

  RELAY_ERROR(setup(&index));

  RUN_QUERY("three");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(0, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 1));
  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(1, num_results);

  RELAY_ERROR(wp_index_remove_label(index, "bob", 1));
  RUN_QUERY("three ~bob");
  ASSERT_EQUALS_UINT(0, num_results);


  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}

TEST(adding_and_removing_labels_are_reflected_in_search) {
  wp_index* index;
  wp_query* query;
  uint64_t results[10];
  uint32_t num_results;

  RELAY_ERROR(setup(&index));

  RELAY_ERROR(wp_index_add_label(index, "bob", 1));
  RELAY_ERROR(wp_index_add_label(index, "bob", 2));
  RELAY_ERROR(wp_index_add_label(index, "bob", 3));

  RUN_QUERY("~bob");
  ASSERT_EQUALS_UINT(3, num_results);

  RELAY_ERROR(wp_index_remove_label(index, "bob", 2));
  RUN_QUERY("~bob");
  ASSERT_EQUALS_UINT(2, num_results);

  RELAY_ERROR(wp_index_remove_label(index, "bob", 2));
  RUN_QUERY("~bob");
  ASSERT_EQUALS_UINT(2, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 3));
  RUN_QUERY("~bob");
  ASSERT_EQUALS_UINT(2, num_results);

  RELAY_ERROR(wp_index_add_label(index, "bob", 2));
  RUN_QUERY("~bob");
  ASSERT_EQUALS_UINT(3, num_results);

  RELAY_ERROR(wp_index_add_label(index, "joe", 1));
  RELAY_ERROR(wp_index_add_label(index, "joe", 2));
  RELAY_ERROR(wp_index_add_label(index, "joe", 3));

  RELAY_ERROR(wp_index_remove_label(index, "bob", 1));
  RELAY_ERROR(wp_index_remove_label(index, "bob", 3));
  // now bob is only on 2

  RELAY_ERROR(wp_index_add_label(index, "harry", 1));
  RELAY_ERROR(wp_index_add_label(index, "harry", 2));
  RELAY_ERROR(wp_index_add_label(index, "harry", 3));

  RELAY_ERROR(wp_index_remove_label(index, "joe", 2));
  RELAY_ERROR(wp_index_remove_label(index, "joe", 1));
  // now joe is only on 3

  RELAY_ERROR(wp_index_add_label(index, "dave", 3));
  RELAY_ERROR(wp_index_add_label(index, "dave", 2));
  RELAY_ERROR(wp_index_add_label(index, "dave", 1));

  RELAY_ERROR(wp_index_remove_label(index, "harry", 2));
  RELAY_ERROR(wp_index_remove_label(index, "harry", 3));
  // now harry is only on 1

  // and dave is on everyone

  RUN_QUERY("~dave");
  ASSERT_EQUALS_UINT(3, num_results);

  RUN_QUERY("~dave ~bob");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(2, results[0]);

  RUN_QUERY("~dave ~joe");
  ASSERT_EQUALS_UINT(1, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);

  RUN_QUERY("~joe ~bob ~harry");
  ASSERT_EQUALS_UINT(0, num_results);

  RUN_QUERY("~dave -~harry");
  ASSERT_EQUALS_UINT(2, num_results);
  ASSERT_EQUALS_UINT64(3, results[0]);
  ASSERT_EQUALS_UINT64(2, results[1]);

  RELAY_ERROR(shutdown(index));
  return NO_ERROR;
}
