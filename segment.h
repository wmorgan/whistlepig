#ifndef WP_SEGMENT_H_
#define WP_SEGMENT_H_

// whistlepig segments
// (c) 2011 William Morgan. See COPYING for license terms.
//
// a segment is the basic persistence mechanism for indexed documents.
// each segment contains a string hash and pool, a term hash, a
// postings region, and a separate labels posting region.
//
// segments store documents until MAX_DOCID or MAX_POSTINGS_REGION_SIZE
// are reached. then you have to make a new segment.
//
// labels are stored in a separate postings region because they're stored in a
// different, mutable format. regular text is stored in a compressed format
// that is not amenable to later changes.

#include <pthread.h>

#include "defaults.h"
#include "error.h"
#include "text.h" // just for segment; can move if necessary
#include "mmap-obj.h"

#define WP_SEGMENT_POSTING_REGION_PATH_SUFFIX "pr"

typedef struct segment_info {
  uint32_t segment_version;
  uint32_t num_docs;
  pthread_rwlock_t lock;
} segment_info;

// a segment is a bunch of all these things
typedef struct wp_segment {
  uint8_t idx;
  mmap_obj seginfo;
  mmap_obj stringmap;
  mmap_obj stringpool;
  mmap_obj termhash;
  mmap_obj postings;
  mmap_obj labels;
} wp_segment;

// API methods

// public: does a segment exist with this base pathname?
int wp_segment_exists(const char* pathname_base);

// public: create a segment, raising an error if it already exists
wp_error* wp_segment_create(wp_segment* segment, const char* pathname_base) RAISES_ERROR;

// public: load a segment, raising an error unless it already exists
wp_error* wp_segment_load(wp_segment* segment, const char* pathname_base) RAISES_ERROR;

// public: reload a segment as necessary, in case an external writer has changed the mmap object sizes
wp_error* wp_segment_reload(wp_segment* segment) RAISES_ERROR;

// public: unload a segment
wp_error* wp_segment_unload(wp_segment* s) RAISES_ERROR;

// public: number of docs in a segment
uint64_t wp_segment_num_docs(wp_segment* s);

// public: delete a segment from disk
wp_error* wp_segment_delete(const char* pathname_base) RAISES_ERROR;

// public: lock grabbing and releasing
wp_error* wp_segment_grab_readlock(wp_segment* seg) RAISES_ERROR;
wp_error* wp_segment_grab_writelock(wp_segment* seg) RAISES_ERROR;
wp_error* wp_segment_release_lock(wp_segment* seg) RAISES_ERROR;

// private: read a posting from the postings region at a given offset
wp_error* wp_segment_read_posting(wp_segment* s, uint32_t offset, posting* po, int include_positions) RAISES_ERROR;

// public: add a posting. be sure you've called wp_segment_ensure_fit with the
// size of the postings list entry before doing this!
wp_error* wp_segment_add_posting(wp_segment* s, const char* field, const char* word, docid_t doc_id, uint32_t num_positions, pos_t positions[]) RAISES_ERROR;

wp_error* wp_segment_add_label(wp_segment* s, const char* label, docid_t doc_id) RAISES_ERROR;
wp_error* wp_segment_remove_label(wp_segment* s, const char* label, docid_t doc_id) RAISES_ERROR;

// public: get a new docid
wp_error* wp_segment_grab_docid(wp_segment* s, docid_t* docid) RAISES_ERROR;

// public: dump a lot of info about the segment to a stream
wp_error* wp_segment_dumpinfo(wp_segment* s, FILE* stream) RAISES_ERROR;

// public: ensure that adding a certain number of postings bytes and label
// postings bytes will still fit within the bounds of the segment. sets success
// to 1 if true or 0 if false. if false, you should put that stuff in a new
// segment.
wp_error* wp_segment_ensure_fit(wp_segment* seg, uint32_t postings_bytes, uint32_t label_bytes, int* success) RAISES_ERROR;

// private: return the size on disk of a position array
wp_error* wp_segment_sizeof_posarray(wp_segment* seg, uint32_t num_positions, pos_t* positions, uint32_t* size) RAISES_ERROR;

// private: count the number of occurences of a particular term
wp_error* wp_segment_count_term(wp_segment* seg, const char* field, const char* term, uint32_t* num_results);

#endif
