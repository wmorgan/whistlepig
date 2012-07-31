#include <stdio.h>
#include "whistlepig.h"
#include "timer.h"

#define MAX_LINE_LENGTH 1024
#define NUM_RESULTS_PER_QUERY 10

int main(int argc, char* argv[]) {
  wp_index* index;

  if(argc != 3) {
    fprintf(stderr, "Usage: %s <index basepath> <query corpus>\n", argv[0]);
    return -1;
  }

  char buf[MAX_LINE_LENGTH];
  wp_query** queries = malloc(sizeof(wp_query*));
  int num_queries = 0;
  int array_size = 1;

  FILE* f = fopen(argv[2], "r");
  while(!feof(f)) {
    if(num_queries >= array_size) {
      array_size = array_size * 2;
      queries = realloc(queries, sizeof(wp_query*) * array_size);
    }

    char* ret = fgets(buf, MAX_LINE_LENGTH, f);
    if(ret == NULL) break;
    DIE_IF_ERROR(wp_query_parse(buf, "body", &queries[num_queries]));
    num_queries++;
  }

  printf("read %d queries\n", num_queries);

  uint64_t results[NUM_RESULTS_PER_QUERY];
  uint32_t num_results_found;
  uint32_t num_iters = 0;
  uint64_t* per_query_times = calloc(num_queries, sizeof(uint64_t));
  for(int i = 0; i < num_queries; i++) per_query_times[i] = 0;

  DIE_IF_ERROR(wp_index_load(&index, argv[1]));

  START_TIMER(total);
  START_TIMER(chunk);
  while(1) {
    num_iters++;
    for(int i = 0; i < num_queries; i++) {
      START_TIMER(query);
      DIE_IF_ERROR(wp_index_setup_query(index, queries[i]));
      DIE_IF_ERROR(wp_index_run_query(index, queries[i], NUM_RESULTS_PER_QUERY, &num_results_found, results));
      DIE_IF_ERROR(wp_index_teardown_query(index, queries[i]));
      MARK_TIMER(query);
      per_query_times[i] += TIMER_MS(query);
    }

    MARK_TIMER(total);
    MARK_TIMER(chunk);
    if(TIMER_MS(chunk) > 1000) {
      for(int i = 0; i < num_queries; i++) {
        wp_query_to_s(queries[i], MAX_LINE_LENGTH, buf);
        printf("%10.1f qps: %s\n", (float)num_iters / (float)per_query_times[i] * 1000.0, buf);
      }
      printf("overall, ran %u queries in %.1fs = %.1f qps\n\n", num_iters, (float)TIMER_MS(total) / 1000.0, (float)(num_queries * num_iters) / (float)TIMER_MS(total) * 1000.0);
      RESET_TIMER(chunk);
    }

    if(num_iters >= 500) break;
  }

  DIE_IF_ERROR(wp_index_unload(index));

  return 0;
}

