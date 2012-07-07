#include <stdio.h>
#include "whistlepig.h"
#include "text.h"
#include "label.h"
#include "stringmap.h"

#define isempty(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&2)
#define isdel(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&1)
#define KEY(h, i) &(h->pool[h->keys[i]])

RAISING_STATIC(dump_postings_list(wp_segment* s, postings_list_header* plh)) {
  posting po;
  docid_t last_doc_id = 0;
  int started = 0;

  postings_region* pr = MMAP_OBJ(s->postings, postings_region);

  uint32_t block_offset = plh->next_offset;
  printf("[%u entries]\n", plh->count);

  while(block_offset != OFFSET_NONE) {
    postings_block* block = wp_postings_block_at(pr, block_offset);
    uint32_t posting_offset = block->postings_head;

    while(posting_offset < block->size) {
      uint32_t next_posting_offset = OFFSET_NONE;
      RELAY_ERROR(wp_text_postings_region_read_posting_from_block(pr, block, posting_offset, &next_posting_offset, &po, 1));

      printf("  [block %u] @%u doc %u:", block_offset, posting_offset, po.doc_id);
      for(uint32_t i = 0; i < po.num_positions; i++) printf(" %d", po.positions[i]);

      if((po.doc_id == 0) || (started && (po.doc_id >= last_doc_id))) printf(" <-- BROKEN");
      started = 1;
      last_doc_id = po.doc_id;
      printf("\n");

      posting_offset = next_posting_offset;
      free(po.positions);
    }

    block_offset = block->prev_block_offset;
  }

  return NO_ERROR;
}

RAISING_STATIC(dump_label_postings_list(wp_segment* s, postings_list_header* plh)) {
  label_posting po;
  docid_t last_doc_id = 0;
  int started = 0;

  postings_region* lpr = MMAP_OBJ(s->labels, postings_region);

  uint32_t offset = plh->next_offset;
  printf("[%u entries]\n", plh->count);

  while(offset != OFFSET_NONE) {
    RELAY_ERROR(wp_label_postings_region_read_label(lpr, offset, &po));

    printf("  @%u doc %u", offset, po.doc_id);
    if((po.doc_id == 0) || (started && (po.doc_id >= last_doc_id))) printf(" <-- BROKEN");
    started = 1;
    last_doc_id = po.doc_id;
    printf("\n");

    offset = po.next_offset;
  }

  return NO_ERROR;
}

RAISING_STATIC(dump(wp_segment* segment)) {
  termhash* th = MMAP_OBJ(segment->termhash, termhash);
  stringmap* sh = MMAP_OBJ(segment->stringmap, stringmap);
  stringpool* sp = MMAP_OBJ(segment->stringpool, stringpool);

  uint32_t* thflags = TERMHASH_FLAGS(th);
  term* thkeys = TERMHASH_KEYS(th);
  postings_list_header* thvals = TERMHASH_VALS(th);

  for(uint32_t i = 0; i < th->n_buckets; i++) {
    if(isempty(thflags, i)); // do nothing
    else if(isdel(thflags, i)) printf("%u: [deleted]", i);
    else {
      term t = thkeys[i];

      if(t.field_s == 0) { // sentinel label value
        if(t.word_s == 0) { // sentinel dead list value
          printf("%u: (dead list)\n", i);
        }
        else {
          const char* label = stringmap_int_to_string(sh, sp, t.word_s);
          printf("%u: ~%s\n", i, label);
        }
        RELAY_ERROR(dump_label_postings_list(segment, &thvals[i]));
      }
      else {
        const char* field = stringmap_int_to_string(sh, sp, t.field_s);
        const char* word = stringmap_int_to_string(sh, sp, t.word_s);
        printf("%u: %s:'%s'\n", i, field, word);
        RELAY_ERROR(dump_postings_list(segment, &thvals[i]));
      }
    }
  }

  return NO_ERROR;
}

int main(int argc, char* argv[]) {
  if(argc != 2) {
    fprintf(stderr, "Usage: %s <segment filename>\n", argv[0]);
    return -1;
  }

  wp_index* index;
  DIE_IF_ERROR(wp_index_load(&index, argv[1]));
  DIE_IF_ERROR(wp_index_dumpinfo(index, stdout));

  for(int i = 0; i < index->num_segments; i++) {
    printf("\nsegment %d details:\n", i);
    DIE_IF_ERROR(dump(&index->segments[i]));
  }

  DIE_IF_ERROR(wp_index_unload(index));

  return 0;
}

