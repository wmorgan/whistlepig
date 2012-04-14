#include <inttypes.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "whistlepig.h"

#define PATH_BUF_SIZE 4096
#define INDEX_VERSION 1

int wp_index_exists(const char* pathname_base) {
  char buf[PATH_BUF_SIZE];
  snprintf(buf, PATH_BUF_SIZE, "%s0", pathname_base);
  return wp_segment_exists(buf);
}

RAISING_STATIC(grab_writelock(wp_index* index)) {
  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  RELAY_ERROR(wp_lock_grab(&ii->lock, WP_LOCK_WRITELOCK));
  return NO_ERROR;
}

RAISING_STATIC(grab_readlock(wp_index* index)) {
  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  RELAY_ERROR(wp_lock_grab(&ii->lock, WP_LOCK_READLOCK));
  return NO_ERROR;
}

RAISING_STATIC(release_lock(wp_index* index)) {
  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  RELAY_ERROR(wp_lock_release(&ii->lock));
  return NO_ERROR;
}


RAISING_STATIC(index_info_init(index_info* ii, uint32_t index_version)) {
  ii->index_version = index_version;
  ii->num_segments = 0;

  RELAY_ERROR(wp_lock_setup(&ii->lock));
  return NO_ERROR;
}

RAISING_STATIC(index_info_validate(index_info* ii, uint32_t index_version)) {
  if(ii->index_version != index_version) RAISE_ERROR("index has type %u; expecting type %u", ii->index_version, index_version);
  return NO_ERROR;
}

wp_error* wp_index_create(wp_index** indexptr, const char* pathname_base) {
  char buf[PATH_BUF_SIZE];

  wp_index* index = *indexptr = malloc(sizeof(wp_index));
  snprintf(buf, PATH_BUF_SIZE, "%s.ii", pathname_base);
  RELAY_ERROR(mmap_obj_create(&index->indexinfo, "wp/indexinfo", buf, sizeof(index_info)));
  RELAY_ERROR(index_info_init(MMAP_OBJ(index->indexinfo, index_info), INDEX_VERSION));

  index->pathname_base = pathname_base;
  index->sizeof_segments = 1;
  index->open = 1;
  index->segments = malloc(sizeof(wp_segment));
  index->docid_offsets = malloc(sizeof(uint64_t));

  snprintf(buf, PATH_BUF_SIZE, "%s0", pathname_base);
  RELAY_ERROR(wp_segment_create(&index->segments[0], buf));
  index->docid_offsets[0] = 0;
  index->num_segments = 1;

  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  ii->num_segments = 1;

  return NO_ERROR;
}

// increases the index->segments array until we have enough
// space to represent index->num_segments
RAISING_STATIC(ensure_segment_pointer_fit(wp_index* index)) {
  if(index->num_segments >= index->sizeof_segments) {
    if(index->sizeof_segments == 0) index->sizeof_segments = 1; // lame
    while(index->sizeof_segments < index->num_segments) index->sizeof_segments *= 2; // lame
    index->segments = realloc(index->segments, sizeof(wp_segment) * index->sizeof_segments);
    index->docid_offsets = realloc(index->docid_offsets, sizeof(uint64_t) * index->sizeof_segments);
    if(index->segments == NULL) RAISE_ERROR("oom");
    if(index->segments == NULL) RAISE_ERROR("oom");
  }

  return NO_ERROR;
}

// ensures that we know about all segments. should be wrapped
// in a global read mutex to prevent creation.
RAISING_STATIC(ensure_all_segments(wp_index* index)) {
  char buf[PATH_BUF_SIZE];

  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  if(ii->num_segments < index->num_segments) RAISE_ERROR("invalid value for num_segments: %u vs %u", index->num_segments, ii->num_segments);
  if(ii->num_segments == index->num_segments) return NO_ERROR;

  // otherwise, we need to load some more segments
  uint16_t old_num_segments = index->num_segments;
  index->num_segments = ii->num_segments;
  RELAY_ERROR(ensure_segment_pointer_fit(index));

  for(uint16_t i = old_num_segments; i < index->num_segments; i++) {
    snprintf(buf, PATH_BUF_SIZE, "%s%u", index->pathname_base, i);
    DEBUG("trying to loading segment %u from %s", i, buf);
    RELAY_ERROR(wp_segment_load(&index->segments[i], buf));

    if(i == 0) index->docid_offsets[i] = 0;
    else {
      // segments return docids 1 through N, so the num_docs in a segment is
      // also the max document id
      segment_info* prevsi = MMAP_OBJ(index->segments[i - 1].seginfo, segment_info);
      index->docid_offsets[i] = prevsi->num_docs + index->docid_offsets[i - 1];
    }
  }

  return NO_ERROR;
}

wp_error* wp_index_load(wp_index** indexptr, const char* pathname_base) {
  char buf[PATH_BUF_SIZE];

  wp_index* index = *indexptr = malloc(sizeof(wp_index));
  snprintf(buf, PATH_BUF_SIZE, "%s.ii", pathname_base);
  RELAY_ERROR(mmap_obj_load(&index->indexinfo, "wp/indexinfo", buf));
  RELAY_ERROR(index_info_validate(MMAP_OBJ(index->indexinfo, index_info), INDEX_VERSION));

  index->pathname_base = pathname_base;
  index->open = 1;
  index->num_segments = 0;
  index->sizeof_segments = 0;
  index->segments = NULL;
  index->docid_offsets = NULL;

  RELAY_ERROR(ensure_all_segments(index));

  return NO_ERROR;
}

// we have two special values at our disposal to mark where we are in
// the sequence of segments
#define SEGMENT_UNINITIALIZED WP_MAX_SEGMENTS
#define SEGMENT_DONE (WP_MAX_SEGMENTS + 1)

wp_error* wp_index_setup_query(wp_index* index, wp_query* query) {
  (void)index;
  query->segment_idx = SEGMENT_UNINITIALIZED;

  return NO_ERROR;
}

// can be called multiple times to resume
wp_error* wp_index_run_query(wp_index* index, wp_query* query, uint32_t max_num_results, uint32_t* num_results, uint64_t* results) {
  *num_results = 0;

  // make sure we have know about all segments (one could've been added by a writer)
  RELAY_ERROR(grab_readlock(index));
  RELAY_ERROR(ensure_all_segments(index));
  RELAY_ERROR(release_lock(index));

  if(index->num_segments == 0) return NO_ERROR;

  if(query->segment_idx == SEGMENT_UNINITIALIZED) {
    query->segment_idx = index->num_segments - 1;
    DEBUG("setting up segment %u", query->segment_idx);
    wp_segment* seg = &index->segments[query->segment_idx];
    RELAY_ERROR(wp_segment_grab_readlock(seg));
    RELAY_ERROR(wp_segment_reload(seg));
    RELAY_ERROR(wp_search_init_search_state(query, seg));
    RELAY_ERROR(wp_segment_release_lock(seg));
  }

  // at this point, we assume we're initialized and query->segment_idx is the index
  // of the segment we're searching against
  while((*num_results < max_num_results) && (query->segment_idx != SEGMENT_DONE)) {
    uint32_t want_num_results = max_num_results - *num_results;
    uint32_t got_num_results = 0;
    search_result* segment_results = malloc(sizeof(search_result) * want_num_results);

    DEBUG("searching segment %d", query->segment_idx);
    wp_segment* seg = &index->segments[query->segment_idx];
    RELAY_ERROR(wp_segment_grab_readlock(seg));
    RELAY_ERROR(wp_segment_reload(seg));
    RELAY_ERROR(wp_search_run_query_on_segment(query, seg, want_num_results, &got_num_results, segment_results));
    RELAY_ERROR(wp_segment_release_lock(seg));
    DEBUG("asked segment %d for %d results, got %d", query->segment_idx, want_num_results, got_num_results);

    // extract the per-segment docids from the search results and adjust by
    // each segment's docid offset to form global docids
    for(uint32_t i = 0; i < got_num_results; i++) {
      results[*num_results + i] = index->docid_offsets[query->segment_idx] + segment_results[i].doc_id;
      wp_search_result_free(&segment_results[i]);
    }
    free(segment_results);
    *num_results += got_num_results;

    if(got_num_results < want_num_results) { // this segment is finished; move to the next one
      DEBUG("releasing index %d", query->segment_idx);
      RELAY_ERROR(wp_search_release_search_state(query));
      if(query->segment_idx > 0) {
        query->segment_idx--;
        DEBUG("setting up index %d", query->segment_idx);
        RELAY_ERROR(wp_search_init_search_state(query, &index->segments[query->segment_idx]));
      }
      else query->segment_idx = SEGMENT_DONE;
    }
  }

  return NO_ERROR;
}

#define RESULT_BUF_SIZE 1024
// count the results by just running the query until it stops. slow!
wp_error* wp_index_count_results(wp_index* index, wp_query* query, uint32_t* num_results) {
  uint64_t results[RESULT_BUF_SIZE];

  *num_results = 0;
  RELAY_ERROR(wp_index_setup_query(index, query));
  while(1) {
    uint32_t this_num_results;
    RELAY_ERROR(wp_index_run_query(index, query, RESULT_BUF_SIZE, &this_num_results, results));
    *num_results += this_num_results;
    if(this_num_results < RESULT_BUF_SIZE) break; // done
  }

  RELAY_ERROR(wp_index_teardown_query(index, query));

  return NO_ERROR;
}

wp_error* wp_index_teardown_query(wp_index* index, wp_query* query) {
  (void)index;
  if((query->segment_idx != SEGMENT_UNINITIALIZED) && (query->segment_idx != SEGMENT_DONE)) {
    RELAY_ERROR(wp_search_release_search_state(query));
  }
  query->segment_idx = SEGMENT_UNINITIALIZED;

  return NO_ERROR;
}

RAISING_STATIC(get_and_writelock_last_segment(wp_index* index, wp_entry* entry, wp_segment** returned_seg)) {
  // assume we have a writelock on the index object here, so that no one can
  // add segments while we're doing this stuff.

  int success;
  RELAY_ERROR(ensure_all_segments(index)); // make sure we know about all segments
  wp_segment* seg = &index->segments[index->num_segments - 1]; // get last segment
  RELAY_ERROR(wp_segment_grab_writelock(seg)); // grab the writelock
  uint32_t postings_bytes; // calculate how much space we'll need to fit this entry in there
  RELAY_ERROR(wp_entry_sizeof_postings_region(entry, seg, &postings_bytes));
  RELAY_ERROR(wp_segment_ensure_fit(seg, postings_bytes, 0, &success));

  // if we can fit in there, then return it! (still locked)
  if(success) {
    *returned_seg = seg;
    return NO_ERROR;
  }

  // otherwise, unlock it and let's make a new one
  RELAY_ERROR(wp_segment_release_lock(seg));

  char buf[PATH_BUF_SIZE];
  DEBUG("segment %d is full, loading a new one", index->num_segments - 1);
  snprintf(buf, PATH_BUF_SIZE, "%s%d", index->pathname_base, index->num_segments);

  // increase the two counters
  index_info* ii = MMAP_OBJ(index->indexinfo, index_info);
  ii->num_segments++;
  index->num_segments++;

  // make sure we have a pointer for this guy
  RELAY_ERROR(ensure_segment_pointer_fit(index));

  // create the new segment
  RELAY_ERROR(wp_segment_create(&index->segments[index->num_segments - 1], buf));

  // set the docid_offset
  segment_info* prevsi = MMAP_OBJ(index->segments[index->num_segments - 2].seginfo, segment_info);
  index->docid_offsets[index->num_segments - 1] = prevsi->num_docs + index->docid_offsets[index->num_segments - 2];

  seg = &index->segments[index->num_segments - 1];
  DEBUG("loaded new segment %d at %p", index->num_segments - 1, seg);

  RELAY_ERROR(wp_segment_grab_writelock(seg)); // lock it
  RELAY_ERROR(wp_entry_sizeof_postings_region(entry, seg, &postings_bytes));
  RELAY_ERROR(wp_segment_ensure_fit(seg, postings_bytes, 0, &success));
  if(!success) RAISE_ERROR("can't fit new entry into fresh segment. that's crazy");

  *returned_seg = seg;
  return NO_ERROR;
}

wp_error* wp_index_add_entry(wp_index* index, wp_entry* entry, uint64_t* doc_id) {
  wp_segment* seg = NULL;
  docid_t seg_doc_id;

  // interleaving lock access -- potential for deadlock is high. :(
  RELAY_ERROR(grab_writelock(index)); // grab full-index lock
  RELAY_ERROR(get_and_writelock_last_segment(index, entry, &seg));
  RELAY_ERROR(release_lock(index)); // release full-index lock

  RELAY_ERROR(wp_segment_reload(seg));
  RELAY_ERROR(wp_segment_grab_docid(seg, &seg_doc_id));
  RELAY_ERROR(wp_entry_write_to_segment(entry, seg, seg_doc_id));
  RELAY_ERROR(wp_segment_release_lock(seg));
  *doc_id = seg_doc_id + index->docid_offsets[index->num_segments - 1];

  return NO_ERROR;
}

wp_error* wp_index_unload(wp_index* index) {
  for(uint16_t i = 0; i < index->num_segments; i++) RELAY_ERROR(wp_segment_unload(&index->segments[i]));
  index->open = 0;

  return NO_ERROR;
}

wp_error* wp_index_free(wp_index* index) {
  if(index->open) RELAY_ERROR(wp_index_unload(index));
  free(index->segments);
  free(index->docid_offsets);
  free(index);

  return NO_ERROR;
}

wp_error* wp_index_dumpinfo(wp_index* index, FILE* stream) {
  fprintf(stream, "index has %d segments\n", index->num_segments);
  for(int i = 0; i < index->num_segments; i++) {
    fprintf(stream, "\nsegment %d:\n", i);
    wp_segment* seg = &index->segments[i];
    RELAY_ERROR(wp_segment_grab_readlock(seg));
    RELAY_ERROR(wp_segment_reload(seg));
    RELAY_ERROR(wp_segment_dumpinfo(seg, stream));
    RELAY_ERROR(wp_segment_release_lock(seg));
  }

  return NO_ERROR;
}

wp_error* wp_index_delete(const char* pathname_base) {
  char buf[PATH_BUF_SIZE];

  int i = 0;
  while(1) {
    snprintf(buf, PATH_BUF_SIZE, "%s%d", pathname_base, i);
    if(wp_segment_exists(buf)) {
      DEBUG("deleting segment %s", buf);
      RELAY_ERROR(wp_segment_delete(buf));
      i++;
    }
    else break;
  }

  snprintf(buf, PATH_BUF_SIZE, "%s.ii", pathname_base);
  unlink(buf);

  return NO_ERROR;
}

wp_error* wp_index_add_label(wp_index* index, const char* label, uint64_t doc_id) {
  int found = 0;

  RELAY_ERROR(grab_writelock(index));
  RELAY_ERROR(ensure_all_segments(index));
  RELAY_ERROR(release_lock(index));

  for(uint32_t i = index->num_segments; i > 0; i--) {
    if(doc_id > index->docid_offsets[i - 1]) {
      wp_segment* seg = &index->segments[i - 1];

      DEBUG("found doc %"PRIu64" in segment %u", doc_id, i - 1);
      RELAY_ERROR(wp_segment_grab_writelock(seg));
      RELAY_ERROR(wp_segment_reload(seg));
      RELAY_ERROR(wp_segment_add_label(seg, label, (docid_t)(doc_id - index->docid_offsets[i - 1])));
      RELAY_ERROR(wp_segment_release_lock(seg));
      found = 1;
      break;
    }
    else DEBUG("did not find doc %"PRIu64" in segment %u", doc_id, i - 1);
  }

  if(!found) RAISE_ERROR("couldn't find doc id %"PRIu64, doc_id);

  return NO_ERROR;
}

wp_error* wp_index_remove_label(wp_index* index, const char* label, uint64_t doc_id) {
  int found = 0;

  RELAY_ERROR(grab_writelock(index));
  RELAY_ERROR(ensure_all_segments(index));
  RELAY_ERROR(release_lock(index));

  for(uint32_t i = index->num_segments; i > 0; i--) {
    if(doc_id > index->docid_offsets[i - 1]) {
      wp_segment* seg = &index->segments[i - 1];

      DEBUG("found doc %"PRIu64" in segment %u", doc_id, i - 1);
      RELAY_ERROR(wp_segment_grab_writelock(seg));
      RELAY_ERROR(wp_segment_reload(seg));
      RELAY_ERROR(wp_segment_remove_label(seg, label, (docid_t)(doc_id - index->docid_offsets[i - 1])));
      RELAY_ERROR(wp_segment_release_lock(seg));
      found = 1;
      break;
    }
    else DEBUG("did not find doc %"PRIu64" in segment %u", doc_id, i - 1);
  }

  if(!found) RAISE_ERROR("couldn't find doc id %"PRIu64, doc_id);

  return NO_ERROR;
}

wp_error* wp_index_num_docs(wp_index* index, uint64_t* num_docs) {
  *num_docs = 0;

  RELAY_ERROR(grab_readlock(index));
  RELAY_ERROR(ensure_all_segments(index));
  RELAY_ERROR(release_lock(index));

  // TODO check for overflow or some shit
  for(uint32_t i = index->num_segments; i > 0; i--) {
    wp_segment* seg = &index->segments[i - 1];
    RELAY_ERROR(wp_segment_grab_readlock(seg));
    RELAY_ERROR(wp_segment_reload(seg));
    *num_docs += wp_segment_num_docs(seg);
    RELAY_ERROR(wp_segment_release_lock(seg));
  }

  return NO_ERROR;
}

// insane. but i'm putting this here. not defined in c99. don't want to make a
// "utils.c" or "compat.c" or whatever just yet.
char* strdup(const char* old) {
  size_t len = strlen(old) + 1;
  char *new = malloc(len * sizeof(char));
  return memcpy(new, old, len);
}
