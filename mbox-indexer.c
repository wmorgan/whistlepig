#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <strings.h>
#include <ctype.h>
#include "whistlepig.h"
#include "timer.h"

#define BUF_SIZE 2048

static void make_lowercase(char* c, size_t len) {
  for(size_t i = 0; i < len; i++) c[i] = tolower(c[i]);
}

int main(int argc, char* argv[]) {
  wp_index* idx;

  if(argc != 3) {
    fprintf(stderr, "Usage: %s <index basepath> <mbox path>\n", argv[0]);
    return -1;
  }

  FILE* mbox = fopen(argv[2], "r");
  if(mbox == NULL) {
    fprintf(stderr, "can't open %s for reading: %s\n", argv[1], strerror(errno));
    return -1;
  }

  char offset_path[1024];
  snprintf(offset_path, 1024, "%s.of", argv[1]);

  FILE* offsets = fopen(offset_path, "w");
  if(offsets == NULL) {
    fprintf(stderr, "can't open %s for writing: %s\n", offset_path, strerror(errno));
    return -1;
  }

  if(wp_index_exists(argv[1])) DIE_IF_ERROR(wp_index_load(&idx, argv[1]));
  else DIE_IF_ERROR(wp_index_create(&idx, argv[1]));

  printf("starting...\n");
  unsigned long total_bytes = 0;
  unsigned long chunk_bytes = 0;
  START_TIMER(total);
  START_TIMER(chunk);

  while(!feof(mbox)) {
    char buf[BUF_SIZE];
    wp_entry* entry = wp_entry_new();
    long start_offset = ftell(mbox);

    char* ignore = fgets(buf, BUF_SIZE, mbox); // skip first line
    (void)ignore;

    // read the entire email into memory
    while(1) {
      long before_read_pos = ftell(mbox);
      char* ret = fgets(buf, BUF_SIZE, mbox);
      if(ret == NULL) {
        if(ferror(mbox)) {
          perror("error reading mbox");
          exit(-1);
        }
        break;
      }
      long after_read_pos = ftell(mbox);

      long len = after_read_pos - before_read_pos;
      total_bytes += len;
      chunk_bytes += len;

      if(!strncmp(buf, "From ", 5)) {
        fseek(mbox, before_read_pos, SEEK_SET);
        break;
      }

      make_lowercase(buf, len);
      DIE_IF_ERROR(wp_entry_add_string(entry, "body", buf));

      MARK_TIMER(chunk);
      if(TIMER_MS(chunk) > 1000) {
        MARK_TIMER(total);
  #define STUFF(k, t) k / 1024, t / 1000.0, ((float)(k) / 1024.0) / ((float)(t) / 1000.0)
        fprintf(stderr, "processed %5luk in %3.1fs = %6.1fk/s. total: %5luk in %5.1fs = %6.1fk/s\n", STUFF(chunk_bytes, TIMER_MS(chunk)), STUFF(total_bytes, TIMER_MS(total)));
        RESET_TIMER(chunk);
        chunk_bytes = 0;
      }
    }

    long end_offset = ftell(mbox);
    if(end_offset > start_offset) {
      size_t dummy_size;
      uint64_t doc_id;
      DIE_IF_ERROR(wp_index_add_entry(idx, entry, &doc_id));
      DIE_IF_ERROR(wp_index_add_label(idx, "inbox", doc_id));
      DIE_IF_ERROR(wp_entry_free(entry));

      // write the offset info
      dummy_size = fwrite(&doc_id, sizeof(doc_id), 1, offsets);
      dummy_size = fwrite(&start_offset, sizeof(start_offset), 1, offsets);
      dummy_size = fwrite(&end_offset, sizeof(end_offset), 1, offsets);
      //printf("doc %u is from %ld--%ld\n", doc_id, start_offset, end_offset);
    }
  }

  fclose(mbox);
  fclose(offsets);

  MARK_TIMER(total);
  fprintf(stderr, "In total, processed %luk in %.1fs = %.1fk/s\n", STUFF(total_bytes, TIMER_MS(total)));

  DIE_IF_ERROR(wp_index_unload(idx));
  return 0;
}

