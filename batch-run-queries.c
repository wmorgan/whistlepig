#include <stdio.h>
#include "whistlepig.h"
#include "timer.h"

int main(int argc, char* argv[]) {
  wp_index* index;
  wp_error* e;

  if(argc != 2) {
    fprintf(stderr, "Usage: %s <index basepath>\n", argv[0]);
    return -1;
  }

  DIE_IF_ERROR(wp_index_load(&index, argv[1]));

  while(1) {
    char input[1024];
    TIMER(query);
    uint32_t total_num_results;
    wp_query* query;

#define HANDLE_ERROR(v) e = v; if(e != NULL) { PRINT_ERROR(e, stdout); wp_error_free(e); continue; }

    printf("query: ");
    fflush(stdout);

    input[0] = 0;
    if(fgets(input, 1024, stdin) == NULL) break;
    if(input[0] == '\0') break;

    HANDLE_ERROR(wp_query_parse(input, "body", &query));
    if(query == NULL) continue;

    RESET_TIMER(query);
    HANDLE_ERROR(wp_index_count_results(index, query, &total_num_results));
    MARK_TIMER(query);
    wp_query_free(query);
    printf("found %d results in %.1fms\n", total_num_results, (float)TIMER_MS(query));
  }

  DIE_IF_ERROR(wp_index_unload(index));

  return 0;
}
