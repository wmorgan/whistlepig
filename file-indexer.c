#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "timer.h"
#include "whistlepig.h"

int main(int argc, char* argv[]) {
  wp_index* index;

  if(argc < 2) {
    fprintf(stderr, "Usage: %s <filename>+\n", argv[0]);
    return -1;
  }

  DIE_IF_ERROR(wp_index_create(&index, "index"));
  //wp_index_dumpinfo(index, stdout);

  printf("starting...\n");
  unsigned long total_bytes = 0;
  unsigned long chunk_bytes = 0;
  START_TIMER(total);
  START_TIMER(chunk);

  for(int i = 1; i < argc; i++) {
    FILE* f = fopen(argv[i], "r");
    if(f == NULL) {
      fprintf(stderr, "can't open %s: %s\n", argv[i], strerror(errno));
      break;
    }

    uint64_t doc_id;
    wp_entry* entry = wp_entry_new();
    DIE_IF_ERROR(wp_entry_add_file(entry, "body", f));
    DIE_IF_ERROR(wp_index_add_entry(index, entry, &doc_id));
    DIE_IF_ERROR(wp_entry_free(entry));
    fclose(f);

    struct stat fstat;
    stat(argv[i], &fstat);
    total_bytes += fstat.st_size;
    chunk_bytes += fstat.st_size;

    MARK_TIMER(chunk);
    if(TIMER_MS(chunk) > 1000) {
      MARK_TIMER(total);
#define STUFF(k, t) k / 1024, t / 1000.0, ((float)(k) / 1024.0) / ((float)(t) / 1000.0)
      fprintf(stderr, "processed %5luk in %3.1fs = %6.1fk/s. total: %5luk in %5.1fs = %6.1fk/s\n", STUFF(chunk_bytes, TIMER_MS(chunk)), STUFF(total_bytes, TIMER_MS(total)));
      RESET_TIMER(chunk);
      chunk_bytes = 0;
    }
  }
  //po = MMAP_OBJ(segment.postings, postings_list); // may have moved
  //fprintf(stderr, "after: segment has %d docs and %d postings\n", po->num_docs, po->num_postings);

  MARK_TIMER(total);
  fprintf(stderr, "In total, processed %luk in %.1fs = %.1fk/s\n", STUFF(total_bytes, TIMER_MS(total)));

  DIE_IF_ERROR(wp_index_unload(index));
  return 0;
}
