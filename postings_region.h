#ifndef WP_POSTINGS_REGION_H_
#define WP_POSTINGS_REGION_H_

// whistlepig postings region
// (c) 2011 William Morgan. See COPYING for license terms.
//
// simple postings region code. used by both labels and text postings.

#include "defaults.h"
#include "error.h"
#include "mmap-obj.h"

#define POSTINGS_REGION_TYPE_IMMUTABLE_VBE 1
#define POSTINGS_REGION_TYPE_MUTABLE_NO_POSITIONS 2 // bigger, mutable
#define POSTINGS_REGION_TYPE_IMMUTABLE_VBE_BLOCKS 3

#define MAX_POSTINGS_REGION_SIZE (256*1024*1024) // tweak me

// the header for the label postings region as a whole
typedef struct postings_region {
  uint32_t postings_type_and_flags;
  uint32_t num_postings;
  uint32_t postings_head, postings_tail;
  uint8_t postings[]; // where the label_posting entries go
} postings_region;

// all private

wp_error* wp_postings_region_init(postings_region* pr, uint32_t initial_size, uint32_t type_and_flags) RAISES_ERROR;
wp_error* wp_postings_region_validate(postings_region* pr, uint32_t type_and_flags) RAISES_ERROR;
wp_error* wp_postings_region_ensure_fit(mmap_obj* mmopr, uint32_t new_size, int* success) RAISES_ERROR;

#endif
