#ifndef WP_TEXT_H_
#define WP_TEXT_H_

// whistlepig text posting regions
// (c) 2011 William Morgan. See COPYING for license terms.
//
// this is the reading and writing code for text postings. see label.[ch]
// for the label code.

#include "defaults.h"
#include "postings_region.h"
#include "termhash.h"
#include "error.h"

// a posting entry. used to represent postings when actively working with them.
// the actual structure on disk/mmap memory region is encoded differently. see
// functions below for reading and writing.
typedef struct posting {
  docid_t doc_id;
  uint32_t num_positions;
  pos_t* positions;
} posting;

// docids:
//
// docid 0 is reserved as a sentinel value. real doc ids are between 1 and
// num_docs (in segment_info) inclusive.
//
// docid num_docs + 1 is also a sentinel value, at query time, for negative
// queries. consumers shouldn't encounter this directly.
//
// in encoding docids, we reserve one bit as a marker for single-occurrence
// terms. thus, the logical maximum number of docs per segment is 2^31 - 2 =
// 2,147,483,646.
//
// segments typically are sized well below that many documents for several
// reasons, including nicer distributions of per-segment locks, and limits to
// things like the number of unique terms (see termhash.h).

#define OFFSET_NONE (uint32_t)0
#define DOCID_NONE (docid_t)0

#define MAX_LOGICAL_DOCID 2147483646 // don't tweak me

// a postings block. stores a sequence of postings for a given term.
// the encoding is roughly like this:
// - these bytes are sequences of (docid, term ids for that doc id), encoded in
//   VBE encoding as deltas, with some other encoding tweaks like the reserved
//   bit described above.
// - posting blocks may have different sizes. typically these sizes will
//   increase exponentially as you move down the stream, but you shouldn't rely
//   on this.
// - posting blocks are stored in min-to-max order. so at read time, you have
//   iterate over them backwards in memory.
// - so, if you're looking for a particular doc that's less than min_docid,
//   you can just skip to the previous postings block.
// - within a block, postings are stored max-to-min order, so reads go forward
//   in memory. unused space is at the beginning of the block. all reads
//   should start at postings_head.
//
// example:
//
//    0 <- [10 - 1] <- [1500 - 51] <- [3900 - 1700] <- "bob"
//
// where "bob" is a term, [B - A] is a posting region containing doc ids A to B,
// <- is the prev_block_offset pointer, and left to right is order in memory.
typedef struct postings_block {
  uint32_t prev_block_offset;
  docid_t max_docid;
  docid_t min_docid;
  uint16_t size; // not including the postings_block header
  uint16_t postings_head;
  uint8_t data[];
} postings_block;

// private
#define wp_postings_block_at(postings_region, offset) ((postings_block*)((((postings_region)->postings) + (offset))))

// private: initialize a text postings region
wp_error* wp_text_postings_region_init(postings_region* pr, uint32_t initial_size) RAISES_ERROR;

// private: validate a text postings region
wp_error* wp_text_postings_region_validate(postings_region* pr) RAISES_ERROR;

// private: read a posting from the postings region at a given offset
wp_error* wp_text_postings_region_read_posting_from_block(postings_region* pr, postings_block* block, uint32_t offset, uint32_t* next_offset, posting* po, int include_positions) RAISES_ERROR;

// private: add a label to an existing document
wp_error* wp_text_postings_region_add_posting(postings_region* pr, posting* po, struct postings_list_header* plh) RAISES_ERROR;

#endif

