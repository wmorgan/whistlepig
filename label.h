#ifndef WP_LABEL_H_
#define WP_LABEL_H_

// whistlepig labels
// (c) 2011 William Morgan. See COPYING for license terms.
//
// code for reading from and writing to the label posting region.
//
// unlike text postings, which are stored in a write-once format, labels are
// stored in an mutable format. currently this is basically linked lists.

#include <pthread.h>

#include "defaults.h"
#include "stringmap.h"
#include "termhash.h"
#include "query.h"
#include "error.h"
#include "search.h"
#include "mmap-obj.h"

// a label posting entry. currently this is also the actual representation of
// label postings on disk, though that may change in the future.
typedef struct label_posting {
  docid_t doc_id;
  uint32_t next_offset;
} label_posting;

#define OFFSET_NONE (uint32_t)0
#define DOCID_NONE (docid_t)0

// the header for the label postings region as a whole
typedef struct label_postings_region {
  uint32_t postings_type_and_flags;
  uint32_t num_postings;
  uint32_t postings_head, postings_tail;
  uint8_t postings[]; // where the label_posting entries go
} label_postings_region;

// all private
wp_error* label_postings_region_init(label_postings_region* pr, uint32_t initial_size) RAISES_ERROR;
wp_error* label_postings_region_validate(label_postings_region* pr) RAISES_ERROR;
wp_error* label_postings_region_ensure_fit(mmap_obj* mmopr, uint32_t postings_bytes, int* success) RAISES_ERROR;

// private: read a label from the label postings region at a given offset
wp_error* wp_segment_read_label(label_postings_region* pr, uint32_t offset, label_posting* po) RAISES_ERROR;

// public: add a label to an existing document
wp_error* wp_segment_add_label(wp_segment* s, const char* label, docid_t doc_id) RAISES_ERROR;

// public: remove a label from an existing document
wp_error* wp_segment_remove_label(wp_segment* s, const char* label, docid_t doc_id) RAISES_ERROR;

#endif

