#include <inttypes.h>
#include <stdio.h>
#include "whistlepig.h"
#include "timer.h"

typedef struct offset {
  long start_offset;
  long end_offset;
} offset;

/*
static void make_lowercase(char* c, int len) {
  for(int i = 0; i < len; i++) c[i] = tolower(c[i]);
}
*/

// map of docids into start/end offsets pairs
KHASH_MAP_INIT_INT64(offsets, offset);

static khash_t(offsets)* load_offsets(const char* base_path) {
  char path[1024];

  snprintf(path, 1024, "%s.of", base_path);

  FILE* f = fopen(path, "r");
  if(f == NULL) {
    fprintf(stderr, "cannot read %s, ignoring\n", path);
    return NULL;
  }

  khash_t(offsets)* h = kh_init(offsets);

  docid_t doc_id;
  while(!feof(f)) {
    offset o;

    size_t read;
    read = fread(&doc_id, sizeof(doc_id), 1, f);
    if(read == 1) read = fread(&o.start_offset, sizeof(long), 1, f);
    if(read == 1) read = fread(&o.end_offset, sizeof(long), 1, f);
    if(read == 1) {
      int ret;
      khiter_t k = kh_put(offsets, h, doc_id, &ret);
      if(!ret) kh_del(offsets, h, k);
      kh_value(h, k) = o;
    }
    else break;
  }

  printf("loaded %d offsets from %s. last was %u\n", kh_size(h), path, doc_id);

  return h;
}

#define RESULTS_TO_SHOW 3

int main(int argc, char* argv[]) {
  wp_index* index;
  wp_error* e;
  khash_t(offsets)* offsets;

  if((argc < 2) || (argc > 3)) {
    fprintf(stderr, "Usage: %s <index basepath> [corpus path]\n", argv[0]);
    return -1;
  }

  offsets = load_offsets(argv[1]);

  DIE_IF_ERROR(wp_index_load(&index, argv[1]));
  //DIE_IF_ERROR(wp_index_dumpinfo(index, stdout));

  FILE* corpus = NULL;
  if(argc == 3) corpus = fopen(argv[2], "r");

  while(1) {
    char input[1024], output[1024];
    uint64_t results[RESULTS_TO_SHOW];
    wp_query* query;
    uint32_t total_num_results;
    TIMER(query);

#define HANDLE_ERROR(v) e = v; if(e != NULL) { PRINT_ERROR(e, stdout); wp_error_free(e); continue; }

    printf("query: ");
    fflush(stdout);

    input[0] = 0;
    if(fgets(input, 1024, stdin) == NULL) break;
    if(input[0] == '\0') break;

    //make_lowercase(input, strlen(input));

    HANDLE_ERROR(wp_query_parse(input, "body", &query));
    if(query == NULL) continue;

    wp_query_to_s(query, 1024, output);
    printf("performing search: %s\n", output);

    RESET_TIMER(query);
    HANDLE_ERROR(wp_index_count_results(index, query, &total_num_results));
    MARK_TIMER(query);
    printf("found %d results in %.1fms\n", total_num_results, (float)TIMER_MS(query));

    if(total_num_results > 0) {
      uint32_t num_results;

      RESET_TIMER(query);
      HANDLE_ERROR(wp_index_setup_query(index, query));
      HANDLE_ERROR(wp_index_run_query(index, query, RESULTS_TO_SHOW, &num_results, results));
      HANDLE_ERROR(wp_index_teardown_query(index, query));
      MARK_TIMER(query);

      printf("retrieved first %d results in %.1fms\n", num_results, (float)TIMER_MS(query));
      for(unsigned int i = 0; i < num_results; i++) {
        //if((unsigned int)argc > i + 1) printf("doc %u -> %s", results[i].doc_id, argv[results[i].doc_id]);
        //else printf("doc %u", results[i].doc_id);

        printf("found doc %"PRIu64"\n", results[i]);

        if(offsets && corpus) {
          khiter_t k = kh_get(offsets, offsets, results[i]);
          if(k != kh_end(offsets)) {
            offset o;
            o = kh_value(offsets, k);

            fseek(corpus, o.start_offset, SEEK_SET);
            long end_offset = o.start_offset + 100;
            if(o.end_offset < end_offset) end_offset = o.end_offset;
            while(ftell(corpus) < end_offset) {
              char buf[1024];
              if(fgets(buf, 1024, corpus) == NULL) break;
              printf("| %s", buf);
            }
          }

          printf("\n");
        }
      }
      printf("\n");
    }

    wp_query_free(query);
    printf("\n");
  }

  DIE_IF_ERROR(wp_index_free(index));
  return 0;
}
