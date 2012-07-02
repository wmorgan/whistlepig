#ifndef WP_POSTING_H_
#define WP_POSTING_H_

// whistlepig posting regions
// (c) 2011 William Morgan. See COPYING for license terms.
//
// this is the reading and writing code for text postings. see label.[ch]
// for the label coe.

#include "defaults.h"
#include "mmap-obj.h"

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

// the header for the postings region as a whole
typedef struct postings_region {
  uint32_t postings_type_and_flags;
  uint32_t num_postings;
  uint32_t postings_head, postings_tail;
  uint8_t postings[]; // where the postings blocks go
} postings_region;

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
//   in memory. unused space is at  the beginning of the block. all reads
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
  docid_t min_docid;
  uint16_t size;
  uint16_t postings_head;
  uint8_t data[];
} postings_block;

typedef struct segment_info {
  uint32_t segment_version;
  uint32_t num_docs;
  pthread_rwlock_t lock;
} segment_info;

wp_error* postings_region_init(postings_region* pr, uint32_t initial_size) RAISES_ERROR;
wp_error* postings_region_validate(postings_region* pr) RAISES_ERROR;

// public: add a posting. be sure you've called wp_segment_ensure_fit with the
// size of the postings list entry before doing this!
wp_error* wp_segment_add_posting(wp_segment* s, const char* field, const char* word, docid_t doc_id, uint32_t num_positions, pos_t positions[]) RAISES_ERROR;

// private: read a posting from the postings region at a given offset
wp_error* wp_segment_read_posting_from_block(wp_segment* s, postings_block* block, uint32_t offset, posting* po, int include_positions) RAISES_ERROR;

#endif

