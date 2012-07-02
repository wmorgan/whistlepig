#ifndef WP_LABEL_H_
#define WP_LABEL_H_

// whistlepig labels
// (c) 2011 William Morgan. See COPYING for license terms.
//
// code for reading from and writing to the label posting region.
//
// unlike text postings, which are stored in a write-once format, labels are
// stored in an mutable format. currently this is basically linked lists.

#include "defaults.h"
#include "postings_region.h"
#include "termhash.h"
#include "error.h"

// a label posting entry. currently this is also the actual representation of
// label postings on disk, though that may change in the future.
typedef struct label_posting {
  docid_t doc_id;
  uint32_t next_offset;
} label_posting;

// private: initialize a label postings region
wp_error* wp_label_postings_region_init(postings_region* pr, uint32_t initial_size) RAISES_ERROR;

// private: validate a label postings region
wp_error* wp_label_postings_region_validate(postings_region* pr) RAISES_ERROR;

// private: read a label from the label postings region at a given offset
wp_error* wp_label_postings_region_read_label(postings_region* pr, uint32_t offset, label_posting* po) RAISES_ERROR;

// private: add a label to an existing document
wp_error* wp_label_postings_region_add_label(postings_region* pr, docid_t doc_id, struct postings_list_header* plh, struct postings_list_header* dead_plh) RAISES_ERROR;

// private: remove a label from an existing document
wp_error* wp_label_postings_region_remove_label(postings_region* pr, docid_t doc_id, struct postings_list_header* plh, struct postings_list_header* dead_plh) RAISES_ERROR;

#endif

