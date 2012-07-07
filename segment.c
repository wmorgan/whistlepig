#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "lock.h"
#include "segment.h"
#include "stringmap.h"
#include "termhash.h"
#include "postings_region.h"
#include "label.h"
#include "text.h"

#define SEGMENT_VERSION 5

static postings_list_header blank_plh = { .count = 0, .next_offset = OFFSET_NONE };
static term dead_term = { .field_s = 0, .word_s = 0 };

wp_error* wp_segment_grab_readlock(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  RELAY_ERROR(wp_lock_grab(&si->lock, WP_LOCK_READLOCK));
  return NO_ERROR;
}

wp_error* wp_segment_grab_writelock(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  RELAY_ERROR(wp_lock_grab(&si->lock, WP_LOCK_WRITELOCK));
  return NO_ERROR;
}

wp_error* wp_segment_release_lock(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  RELAY_ERROR(wp_lock_release(&si->lock));
  return NO_ERROR;
}

wp_error* wp_segment_count_term(wp_segment* seg, const char* field, const char* word, uint32_t* num_results) {
  stringmap* sh = MMAP_OBJ(seg->stringmap, stringmap);
  stringpool* sp = MMAP_OBJ(seg->stringpool, stringpool);
  termhash* th = MMAP_OBJ(seg->termhash, termhash);

  term t;
  if(field == NULL) t.field_s = 0; // label sentinel
  else t.field_s = stringmap_string_to_int(sh, sp, field);
  t.word_s = stringmap_string_to_int(sh, sp, word);

  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) *num_results = 0;
  else *num_results = plh->count;

  return NO_ERROR;
}

RAISING_STATIC(segment_info_init(segment_info* si, uint32_t segment_version)) {
  si->segment_version = segment_version;
  si->num_docs = 0;

  RELAY_ERROR(wp_lock_setup(&si->lock));
  return NO_ERROR;
}

RAISING_STATIC(segment_info_validate(segment_info* si, uint32_t segment_version)) {
  if(si->segment_version != segment_version) RAISE_VERSION_ERROR("segment has type %u; expecting type %u", si->segment_version, segment_version);
  return NO_ERROR;
}

#define INITIAL_POSTINGS_SIZE 2048
#define FN_SIZE 1024

wp_error* wp_segment_load(wp_segment* segment, const char* pathname_base) {
  char fn[FN_SIZE];

  // open the segment info
  snprintf(fn, 128, "%s.si", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->seginfo, "wp/seginfo", fn));
  RELAY_ERROR(segment_info_validate(MMAP_OBJ(segment->seginfo, segment_info), SEGMENT_VERSION));

  // open the string pool
  snprintf(fn, 128, "%s.sp", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->stringpool, "wp/stringpool", fn));

  // open the string hash
  snprintf(fn, 128, "%s.sh", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->stringmap, "wp/stringmap", fn));

  // open the term hash
  snprintf(fn, 128, "%s.th", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->termhash, "wp/termhash", fn));

  // open the postings region
  snprintf(fn, 128, "%s." WP_SEGMENT_POSTING_REGION_PATH_SUFFIX, pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->postings, "wp/postings", fn));
  RELAY_ERROR(wp_text_postings_region_validate(MMAP_OBJ(segment->postings, postings_region)));

  // open the labels postings region
  snprintf(fn, 128, "%s.lb", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->labels, "wp/labels", fn));
  RELAY_ERROR(wp_label_postings_region_validate(MMAP_OBJ(segment->labels, postings_region)));

  return NO_ERROR;
}

wp_error* wp_segment_reload(wp_segment* segment) {
  RELAY_ERROR(mmap_obj_reload(&segment->seginfo));
  RELAY_ERROR(mmap_obj_reload(&segment->stringpool));
  RELAY_ERROR(mmap_obj_reload(&segment->stringmap));
  RELAY_ERROR(mmap_obj_reload(&segment->termhash));
  RELAY_ERROR(mmap_obj_reload(&segment->postings));
  RELAY_ERROR(mmap_obj_reload(&segment->labels));

  return NO_ERROR;
}

wp_error* wp_segment_create(wp_segment* segment, const char* pathname_base) {
  char fn[FN_SIZE];

  // create the segment info
  snprintf(fn, 128, "%s.si", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->seginfo, "wp/seginfo", fn, sizeof(segment_info)));
  RELAY_ERROR(segment_info_init(MMAP_OBJ(segment->seginfo, segment_info), SEGMENT_VERSION));

  // create the string pool
  snprintf(fn, 128, "%s.sp", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->stringpool, "wp/stringpool", fn, stringpool_initial_size()));
  stringpool_init(MMAP_OBJ(segment->stringpool, stringpool));

  // create the string hash
  snprintf(fn, 128, "%s.sh", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->stringmap, "wp/stringmap", fn, stringmap_initial_size()));
  stringmap_init(MMAP_OBJ(segment->stringmap, stringmap));

  // create the term hash
  snprintf(fn, 128, "%s.th", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->termhash, "wp/termhash", fn, termhash_initial_size()));
  termhash_init(MMAP_OBJ(segment->termhash, termhash));

  // create the postings region
  snprintf(fn, 128, "%s." WP_SEGMENT_POSTING_REGION_PATH_SUFFIX, pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->postings, "wp/postings", fn, sizeof(postings_region) + INITIAL_POSTINGS_SIZE));
  RELAY_ERROR(wp_text_postings_region_init(MMAP_OBJ(segment->postings, postings_region), INITIAL_POSTINGS_SIZE));

  // create the labels postings region
  snprintf(fn, 128, "%s.lb", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->labels, "wp/labels", fn, sizeof(postings_region) + INITIAL_POSTINGS_SIZE));
  RELAY_ERROR(wp_label_postings_region_init(MMAP_OBJ(segment->labels, postings_region), INITIAL_POSTINGS_SIZE));

  return NO_ERROR;
}

int wp_segment_exists(const char* pathname_base) {
  struct stat fstat;
  char fn[FN_SIZE];

  snprintf(fn, 128, "%s.sp", pathname_base);
  return !stat(fn, &fstat);
}

wp_error* wp_segment_delete(const char* pathname_base) {
  char fn[FN_SIZE];

  snprintf(fn, 128, "%s.si", pathname_base);
  unlink(fn);
  snprintf(fn, 128, "%s." WP_SEGMENT_POSTING_REGION_PATH_SUFFIX, pathname_base);
  unlink(fn);
  snprintf(fn, 128, "%s.sp", pathname_base);
  unlink(fn);
  snprintf(fn, 128, "%s.sh", pathname_base);
  unlink(fn);
  snprintf(fn, 128, "%s.th", pathname_base);
  unlink(fn);
  snprintf(fn, 128, "%s.lb", pathname_base);
  unlink(fn);

  return NO_ERROR;
}

wp_error* wp_segment_unload(wp_segment* s) {
  RELAY_ERROR(mmap_obj_unload(&s->stringpool));
  RELAY_ERROR(mmap_obj_unload(&s->stringmap));
  RELAY_ERROR(mmap_obj_unload(&s->termhash));
  RELAY_ERROR(mmap_obj_unload(&s->postings));
  RELAY_ERROR(mmap_obj_unload(&s->labels));
  return NO_ERROR;
}

RAISING_STATIC(bump_stringmap(wp_segment* s, int* success)) {
  stringmap* sh = MMAP_OBJ(s->stringmap, stringmap);

  *success = 1;
  if(stringmap_needs_bump(sh)) {
    DEBUG("bumping stringmap size");
    uint32_t next_size = stringmap_next_size(sh);
    if(next_size <= stringmap_size(sh)) {
      DEBUG("stringmap can't be bumped no more!");
      *success = 0;
    }
    else {
      RELAY_ERROR(mmap_obj_resize(&s->stringmap, next_size));
      RELAY_ERROR(stringmap_bump_size(MMAP_OBJ(s->stringmap, stringmap), MMAP_OBJ(s->stringpool, stringpool)));
    }
  }

  return NO_ERROR;
}

RAISING_STATIC(bump_stringpool(wp_segment* s, int* success)) {
  stringpool* sp = MMAP_OBJ(s->stringpool, stringpool);

  *success = 1;
  if(stringpool_needs_bump(sp)) {
    DEBUG("bumping stringpool size");
    uint32_t next_size = stringpool_next_size(sp);
    if(next_size <= stringpool_size(sp)) {
      DEBUG("stringpool can't be bumped no more!");
      *success = 0;
    }
    else {
      RELAY_ERROR(mmap_obj_resize(&s->stringpool, next_size));
      stringpool_bump_size(MMAP_OBJ(s->stringpool, stringpool));
    }
  }

  return NO_ERROR;
}

RAISING_STATIC(bump_termhash(wp_segment* s, int* success)) {
  termhash* th = MMAP_OBJ(s->termhash, termhash);

  *success = 1;
  if(termhash_needs_bump(th)) {
    DEBUG("bumping termhash size");
    uint32_t next_size = termhash_next_size(th);
    if(next_size <= termhash_size(th)) {
      DEBUG("termhash can't be bumped no more!");
      *success = 0;
    }
    else {
      RELAY_ERROR(mmap_obj_resize(&s->termhash, next_size));
      RELAY_ERROR(termhash_bump_size(MMAP_OBJ(s->termhash, termhash)));
      *success = 1;
    }
  }

  return NO_ERROR;
}

RAISING_STATIC(postings_region_ensure_fit(mmap_obj* mmopr, uint32_t postings_bytes, int* success)) {
  postings_region* pr = MMAP_OBJ_PTR(mmopr, postings_region);
  uint32_t new_head = pr->postings_head + postings_bytes;

  DEBUG("ensuring fit for %u postings bytes", postings_bytes);

  uint32_t new_tail = pr->postings_tail;
  while(new_tail <= new_head) new_tail = new_tail * 2;

  if(new_tail > MAX_POSTINGS_REGION_SIZE - sizeof(mmap_obj_header)) new_tail = MAX_POSTINGS_REGION_SIZE - sizeof(mmap_obj_header);
  DEBUG("new tail will be %u, current is %u, max is %u", new_tail, pr->postings_tail, MAX_POSTINGS_REGION_SIZE);

  if(new_tail <= new_head) { // can't increase enough
    *success = 0;
    return NO_ERROR;
  }

  if(new_tail != pr->postings_tail) { // need to resize
    DEBUG("request for %u postings bytes, old tail is %u, new tail will be %u, max is %u\n", postings_bytes, pr->postings_tail, new_tail, MAX_POSTINGS_REGION_SIZE);
    RELAY_ERROR(mmap_obj_resize(mmopr, new_tail));
    pr = MMAP_OBJ_PTR(mmopr, postings_region); // may have changed!
    pr->postings_tail = new_tail;
  }

  *success = 1;
  return NO_ERROR;
}

// TODO make this function take the number of stringpool entries, the number of
// terms, etc rather than just being a heuristic for everything except for the
// postings list
wp_error* wp_segment_ensure_fit(wp_segment* seg, uint32_t postings_bytes, uint32_t label_bytes, int* success) {
  RELAY_ERROR(postings_region_ensure_fit(&seg->postings, postings_bytes, success));
  if(!*success) return NO_ERROR;

  RELAY_ERROR(postings_region_ensure_fit(&seg->labels, label_bytes, success));
  if(!*success) return NO_ERROR;

  RELAY_ERROR(bump_stringmap(seg, success));
  if(!*success) return NO_ERROR;

  RELAY_ERROR(bump_stringpool(seg, success));
  if(!*success) return NO_ERROR;

  RELAY_ERROR(bump_termhash(seg, success));
  if(!*success) return NO_ERROR;

  DEBUG("fit of %u postings bytes ensured", postings_bytes);

  return NO_ERROR;
}

wp_error* wp_segment_add_posting(wp_segment* s, const char* field, const char* word, docid_t doc_id, uint32_t num_positions, pos_t positions[]) {
  int success;
  if(doc_id == 0) RAISE_ERROR("can't add doc 0");

  // TODO move this logic up to ensure_fit()
  RELAY_ERROR(bump_stringmap(s, &success));
  RELAY_ERROR(bump_stringpool(s, &success));
  RELAY_ERROR(bump_termhash(s, &success));

  DEBUG("adding posting for %s:%s and doc %u with %u positions", field, word, doc_id, num_positions);

  postings_region* pr = MMAP_OBJ(s->postings, postings_region);
  stringmap* sh = MMAP_OBJ(s->stringmap, stringmap);
  termhash* th = MMAP_OBJ(s->termhash, termhash);
  stringpool* sp = MMAP_OBJ(s->stringpool, stringpool);

  // construct the term object
  term t;
  RELAY_ERROR(stringmap_add(sh, sp, field, &t.field_s));
  RELAY_ERROR(stringmap_add(sh, sp, word, &t.word_s));

  DEBUG("%s:%s maps to %u:%u", field, word, t.field_s, t.word_s);

  // find the posting-list header
  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, t, &blank_plh));
    plh = termhash_get_val(th, t);
  }
  DEBUG("posting list header for %s:%s is at %p", field, word, plh);

  posting po;
  po.doc_id = doc_id;
  po.num_positions = num_positions;
  po.positions = positions;

  // TODO handle resize exceptions
  RELAY_ERROR(wp_text_postings_region_add_posting(pr, &po, plh));

  return NO_ERROR;
}

wp_error* wp_segment_add_label(wp_segment* s, const char* label, docid_t doc_id) {
  int success;

  if(doc_id == 0) RAISE_ERROR("can't add a label to doc 0");

  // TODO move this logic up to ensure_fit()
  RELAY_ERROR(bump_stringmap(s, &success));
  RELAY_ERROR(bump_stringpool(s, &success));
  RELAY_ERROR(bump_termhash(s, &success));

  DEBUG("adding label '%s' to doc %u", label, doc_id);

  postings_region* pr = MMAP_OBJ(s->labels, postings_region);
  stringmap* sh = MMAP_OBJ(s->stringmap, stringmap);
  termhash* th = MMAP_OBJ(s->termhash, termhash);
  stringpool* sp = MMAP_OBJ(s->stringpool, stringpool);

  // construct the term object. term objects for labels have the special
  // sentinel field value 0
  term t;
  t.field_s = 0; // label sentinel value
  RELAY_ERROR(stringmap_add(sh, sp, label, &t.word_s)); // get word key

  // find the previous and next label postings, between which we'll insert this
  // posting
  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, t, &blank_plh));
    plh = termhash_get_val(th, t);
  }

  // find a space for the posting by first checking for a free postings in the
  // dead list. the dead list is the list stored under the sentinel term with
  // field 0 and word 0.
  postings_list_header* dead_plh = termhash_get_val(th, dead_term);
  if(dead_plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, dead_term, &blank_plh));
    dead_plh = termhash_get_val(th, t);
  }

  RELAY_ERROR(wp_label_postings_region_add_label(pr, doc_id, plh, dead_plh));

  return NO_ERROR;
}

wp_error* wp_segment_remove_label(wp_segment* s, const char* label, docid_t doc_id) {
  int success;

  // TODO move this logic to ensure_fit
  RELAY_ERROR(bump_termhash(s, &success)); // we might add an entry for the dead list

  postings_region* pr = MMAP_OBJ(s->labels, postings_region);
  stringmap* sh = MMAP_OBJ(s->stringmap, stringmap);
  termhash* th = MMAP_OBJ(s->termhash, termhash);
  stringpool* sp = MMAP_OBJ(s->stringpool, stringpool);

  // construct the term object. term objects for labels have the special
  // sentinel field value 0
  term t;
  t.field_s = 0; // label sentinel value
  t.word_s = stringmap_string_to_int(sh, sp, label); // will be -1 if not there

  // find the posting and the previous posting in the list, if any
  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) {
    DEBUG("no such label %s", label);
    return NO_ERROR;
  }

  // get the dead list for later reclamation
  postings_list_header* dead_plh = termhash_get_val(th, dead_term);
  if(dead_plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, dead_term, &blank_plh));
    dead_plh = termhash_get_val(th, t);
  }

  RELAY_ERROR(wp_label_postings_region_remove_label(pr, doc_id, plh, dead_plh));

  return NO_ERROR;
}

wp_error* wp_segment_grab_docid(wp_segment* segment, docid_t* doc_id) {
  segment_info* si = MMAP_OBJ(segment->seginfo, segment_info);
  *doc_id = ++si->num_docs;
  return NO_ERROR;
}

wp_error* wp_segment_dumpinfo(wp_segment* segment, FILE* stream) {
  segment_info* si = MMAP_OBJ(segment->seginfo, segment_info);
  postings_region* pr = MMAP_OBJ(segment->postings, postings_region);
  stringmap* sh = MMAP_OBJ(segment->stringmap, stringmap);
  stringpool* sp = MMAP_OBJ(segment->stringpool, stringpool);
  termhash* th = MMAP_OBJ(segment->termhash, termhash);

  #define p(a, b) 100.0 * (float)a / (float)b

  fprintf(stream, "segment has type %u and version %u\n", pr->postings_type_and_flags, si->segment_version);
  fprintf(stream, "segment has %u docs and %u postings\n", si->num_docs, pr->num_postings);
  fprintf(stream, "postings region is %6ukb at %3.1f%% saturation\n", segment->postings.content->size / 1024, p(pr->postings_head, pr->postings_tail));
  fprintf(stream, "    string hash is %6ukb at %3.1f%% saturation\n", segment->stringmap.content->size / 1024, p(sh->n_occupied, sh->n_buckets));
  fprintf(stream, "     stringpool is %6ukb at %3.1f%% saturation\n", segment->stringpool.content->size / 1024, p(sp->next, sp->size));
  fprintf(stream, "     term hash has %6ukb at %3.1f%% saturation\n", segment->termhash.content->size / 1024, p(th->n_occupied, th->n_buckets));

  return NO_ERROR;
}

uint64_t wp_segment_num_docs(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  return si->num_docs;
}
