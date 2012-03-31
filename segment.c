#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "whistlepig.h"

#define POSTINGS_REGION_TYPE_IMMUTABLE_VBE   1
#define POSTINGS_REGION_TYPE_MUTABLE_NO_POSITIONS 2 // bigger, mutable

#define SEGMENT_VERSION 3

#define wp_segment_label_posting_at(posting_region, offset) ((label_posting*)(posting_region->postings + offset))

static void postings_region_init(postings_region* pr, uint32_t initial_size, uint32_t postings_type_and_flags) {
  pr->postings_type_and_flags = postings_type_and_flags;
  pr->num_postings = 0;
  pr->postings_head = 1; // skip one byte, which is reserved as OFFSET_NONE
  pr->postings_tail = initial_size;
}

RAISING_STATIC(setup_lock(pthread_rwlock_t* lock)) {
  pthread_rwlockattr_t attr;
  if(pthread_rwlockattr_init(&attr) != 0) RAISE_SYSERROR("cannot initialize pthreads rwlock attr");
  if(pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) RAISE_SYSERROR("cannot set pthreads rwlockattr to PTHREAD_PROCESS_SHARED");
  if(pthread_rwlock_init(lock, &attr) != 0) RAISE_SYSERROR("cannot initialize pthreads rwlock");
  if(pthread_rwlockattr_destroy(&attr) != 0) RAISE_SYSERROR("cannot destroy pthreads rwlock attr");

  return NO_ERROR;
}

RAISING_STATIC(segment_info_init(segment_info* si, uint32_t segment_version)) {
  si->segment_version = segment_version;
  si->num_docs = 0;

  RELAY_ERROR(setup_lock(&si->lock));
  return NO_ERROR;
}

#define WP_SEGMENT_LOCK_READLOCK 0
#define WP_SEGMENT_LOCK_WRITELOCK 1

/*

alernative implementation that uses rdlock. doesn't allow us to detect
and break stale locks, but gives us a good sense of what the timing should
be like.

wp_error* wp_segment_grab_lock2(wp_segment* seg, int lock_type) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  const char* lock_name = (lock_type == WP_SEGMENT_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock for segment %p", lock_name, seg);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  int ret = 0;
  switch(lock_type) {
    case WP_SEGMENT_LOCK_READLOCK: ret = pthread_rwlock_rdlock(&si->lock); break;
    case WP_SEGMENT_LOCK_WRITELOCK: ret = pthread_rwlock_wrlock(&si->lock); break;
  }

  if(ret != 0) RAISE_SYSERROR("grabbing %slock", lock_name);

  clock_gettime(CLOCK_MONOTONIC, &end);


  uint64_t diff_in_ns = ((end.tv_sec * 1000000000) + end.tv_nsec) -
             ((start.tv_sec * 1000000000) + start.tv_nsec);

  uint32_t total_delay_ms = diff_in_ns / 1000000;
  if(total_delay_ms > 0) printf("XXX acquired %slock for segment %p after %ums\n", lock_name, seg, total_delay_ms);

  return NO_ERROR;
}
*/

// we will wait this many milliseconds before assuming the lock
// is stale and breaking it.
#define LOCK_STALE_TIME_MS 2500

/* here's the best implementation i can find, empirically, of being
   able to grab pthread read and write locks, while still being able
   to detect stale locks and repair them.

   it involves a busyloop, which is lame.
*/
wp_error* wp_segment_grab_lock(wp_segment* seg, int lock_type) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  const char* lock_name = (lock_type == WP_SEGMENT_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock for segment %p", lock_name, seg);

  unsigned int delay_ms = 1;
  uint32_t total_delay_ms = 0;

  while(1) {
    int ret = 0;

    switch(lock_type) {
      case WP_SEGMENT_LOCK_READLOCK: ret = pthread_rwlock_tryrdlock(&si->lock); break;
      case WP_SEGMENT_LOCK_WRITELOCK: ret = pthread_rwlock_trywrlock(&si->lock); break;
      default: RAISE_ERROR("invalid lock type");
    }

    if(ret == 0) break; // acquired!
    // we get EAGAINs here if the writer died before closing the lock.
    if((ret != EBUSY) && (ret != EAGAIN)) RAISE_SYSERROR("acquiring %slock", lock_name);

    if(total_delay_ms >= LOCK_STALE_TIME_MS) {
      //RAISE_ERROR("timeout acquiring %slock: %ums", lock_name, total_delay_ms);
      DEBUG("assuming lock is stale and breaking it!");
      RELAY_ERROR(setup_lock(&si->lock));
    }
    if(delay_ms > 1000) sleep(delay_ms / 1000);
    usleep(1000 * (delay_ms % 1000));
    total_delay_ms += delay_ms;
  }

  if(total_delay_ms > 0) DEBUG(":( acquired %slock for segment %p after %ums\n", lock_name, seg, total_delay_ms);
  return NO_ERROR;
}

/* an alternative implementation that uses the _timed pthread operations.
   although this should be the best version, i had many problems with it.  the
   timeout didn't seem to ever trigger. i would also see an EINVAL whenever a
   writer had a readlock. in the case of a stale lock, i would just get EINVALS
   forever rather than a proper ETIMEDOUT.

   since using this would require implementing my own stale lock detection
   anyways, so i'm just going to use the simpler version above instead.
*/
/*
wp_error* wp_segment_grab_lock3(wp_segment* seg, int lock_type) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  const char* lock_name = (lock_type == WP_SEGMENT_LOCK_READLOCK ? "read" : "write");
  DEBUG("grabbing %slock for segment %p", lock_name, seg);

  struct timespec timeout;
  timeout.tv_sec = 3;//LOCK_STALE_TIME_MS / 1000;
  timeout.tv_nsec = 0;//(LOCK_STALE_TIME_MS % 1000) * 1000000;

  struct timeval startt, endt;
  gettimeofday(&startt, NULL);

  int acquired = 0;
  while(!acquired) {
    int ret = 0;

    switch(lock_type) {
      case WP_SEGMENT_LOCK_READLOCK: ret = pthread_rwlock_timedrdlock(&si->lock, &timeout); break;
      case WP_SEGMENT_LOCK_WRITELOCK: ret = pthread_rwlock_timedwrlock(&si->lock, &timeout); break;
      default: RAISE_ERROR("invalid lock type");
    }

    switch(ret) {
      case 0: acquired = 1; break;
      case ETIMEDOUT:
        DEBUG("assuming lock is stale and breaking it!");
        RELAY_ERROR(setup_lock(&si->lock));
        break;
      case EAGAIN:
        // despite the documentation, this seems to happen every time we request a readlock and
        // the lock is already held by the writer. so we will just busyloop here. this happens
        // fairly frequently, so this is lame.
        usleep(1000);
        break;
      default:
        RAISE_SYSERROR("acquiring %slock", lock_name);
    }
  }
  gettimeofday(&endt, NULL);
  long elapsed = ((endt.tv_sec - startt.tv_sec) * 1000) + ((endt.tv_usec - startt.tv_usec) / 1000);

  if(elapsed > 0) printf(":( acquired %slock for segment %p after %ldms\n", lock_name, seg, elapsed);
  return NO_ERROR;
}
*/

wp_error* wp_segment_grab_readlock(wp_segment* seg) {
  RELAY_ERROR(wp_segment_grab_lock(seg, WP_SEGMENT_LOCK_READLOCK));
  return NO_ERROR;
}

wp_error* wp_segment_grab_writelock(wp_segment* seg) {
  RELAY_ERROR(wp_segment_grab_lock(seg, WP_SEGMENT_LOCK_WRITELOCK));
  return NO_ERROR;
}

wp_error* wp_segment_release_readlock(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  if(pthread_rwlock_unlock(&si->lock) != 0) RAISE_SYSERROR("releasing segment readlock");
  DEBUG("released read lock for segment %p", seg);
  return NO_ERROR;
}

wp_error* wp_segment_release_writelock(wp_segment* seg) {
  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  if(pthread_rwlock_unlock(&si->lock) != 0) RAISE_SYSERROR("releasing segment writelock");
  DEBUG("released write lock for segment %p", seg);
  return NO_ERROR;
}

RAISING_STATIC(segment_info_validate(segment_info* si, uint32_t segment_version)) {
  if(si->segment_version != segment_version) RAISE_ERROR("segment has type %u; expecting type %u", si->segment_version, segment_version);
  return NO_ERROR;
}

RAISING_STATIC(postings_region_validate(postings_region* pr, uint32_t postings_type_and_flags)) {
  if(pr->postings_type_and_flags != postings_type_and_flags) RAISE_ERROR("postings region has type %u; expecting type %u", pr->postings_type_and_flags, postings_type_and_flags);
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
  snprintf(fn, 128, "%s.sh_", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->stringmap, "wp/stringmap", fn));

  // open the term hash
  snprintf(fn, 128, "%s.th", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->termhash, "wp/termhash", fn));

  // open the postings region
  snprintf(fn, 128, "%s." WP_SEGMENT_POSTING_REGION_PATH_SUFFIX, pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->postings, "wp/postings", fn));
  RELAY_ERROR(postings_region_validate(MMAP_OBJ(segment->postings, postings_region), POSTINGS_REGION_TYPE_IMMUTABLE_VBE));

  // open the labels postings region
  snprintf(fn, 128, "%s.lb", pathname_base);
  RELAY_ERROR(mmap_obj_load(&segment->labels, "wp/labels", fn));
  RELAY_ERROR(postings_region_validate(MMAP_OBJ(segment->labels, postings_region), POSTINGS_REGION_TYPE_MUTABLE_NO_POSITIONS));

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
  snprintf(fn, 128, "%s.sh_", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->stringmap, "wp/stringmap", fn, stringmap_initial_size()));
  stringmap_init(MMAP_OBJ(segment->stringmap, stringmap));

  // create the term hash
  snprintf(fn, 128, "%s.th", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->termhash, "wp/termhash", fn, termhash_initial_size()));
  termhash_init(MMAP_OBJ(segment->termhash, termhash));

  // create the postings region
  snprintf(fn, 128, "%s." WP_SEGMENT_POSTING_REGION_PATH_SUFFIX, pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->postings, "wp/postings", fn, sizeof(postings_region) + INITIAL_POSTINGS_SIZE));
  postings_region_init(MMAP_OBJ(segment->postings, postings_region), INITIAL_POSTINGS_SIZE, POSTINGS_REGION_TYPE_IMMUTABLE_VBE);

  // create the labels postings region
  snprintf(fn, 128, "%s.lb", pathname_base);
  RELAY_ERROR(mmap_obj_create(&segment->labels, "wp/labels", fn, sizeof(postings_region) + INITIAL_POSTINGS_SIZE));
  postings_region_init(MMAP_OBJ(segment->labels, postings_region), INITIAL_POSTINGS_SIZE, POSTINGS_REGION_TYPE_MUTABLE_NO_POSITIONS);

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
  snprintf(fn, 128, "%s.sh_", pathname_base);
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

static uint32_t size_of(uint32_t num_positions, pos_t positions[]) {
  (void)positions;
  uint32_t position_size = (uint32_t)sizeof(pos_t) * num_positions;
  uint32_t size = (uint32_t)sizeof(posting) - (uint32_t)sizeof(pos_t*) + position_size;

  return size;
}

wp_error* wp_segment_sizeof_posarray(wp_segment* seg, uint32_t num_positions, pos_t* positions, uint32_t* size) {
  (void)seg;
  *size = size_of(num_positions, positions);
  return NO_ERROR;
}

#define VALUE_BITMASK 0x7f
RAISING_STATIC(write_multibyte(uint8_t* location, uint32_t val, uint32_t* size)) {
  //printf("xx writing %u to position %p as:\n", val, location);
  uint8_t* start = location;

  while(val > VALUE_BITMASK) {
    uint8_t c = (val & VALUE_BITMASK) | 0x80;
    *location = c;
    //printf("xx %d = %d | %d at %p\n", c, val & BITMASK, 0x80, location);
    location++;
    val >>= 7;
  }
  uint8_t c = (val & VALUE_BITMASK);
  *location = c;
  //printf("xx %d at %p\n", c, location);
  *size = (uint32_t)(location + 1 - start);
  //printf("xx total %u bytes\n", *size);
  return NO_ERROR;
}

RAISING_STATIC(read_multibyte(uint8_t* location, uint32_t* val, uint32_t* size)) {
  uint8_t* start = location;
  uint32_t shift = 0;

  *val = 0;
  while(*location & 0x80) {
    //printf("yy read continue byte %d -> %d at %p\n", *location, *location & ~0x80, location);
    *val |= (*location & ~0x80) << shift;
    shift += 7;
    location++;
  }
  *val |= *location << shift;
  //printf("yy read final byte %d at %p\n", *location, location);
  *size = (uint32_t)(location + 1 - start);
  //printf("yy total %d bytes, val = %d\n\n", *size, *val);
  return NO_ERROR;
}

/* write posting entry using a variable-byte encoding

   unfortunately we can't write doc_id deltas, which is what would really make
   this encoding pay off, because we write the entries in increasing doc_id
   order but read them in decreasing order. so we write doc_ids raw.

   for next_offsets, we write the delta against the current offset. since the
   next_offset is guaranteed to be less than the current offset, we subtract
   next from current.

   positions are written as deltas.
*/

RAISING_STATIC(write_posting(wp_segment* seg, posting* po, pos_t positions[])) {
  postings_region* pr = MMAP_OBJ(seg->postings, postings_region);

  uint32_t size;
  uint32_t offset = pr->postings_head;

  if(po->next_offset >= pr->postings_head) RAISE_ERROR("next_offset %u >= postings_head %u", po->next_offset, pr->postings_head);
  if(po->num_positions == 0) RAISE_ERROR("num_positions == 0");

  uint32_t doc_id = po->doc_id << 1;
  if(po->num_positions == 1) doc_id |= 1; // marker for single postings
  RELAY_ERROR(write_multibyte(&pr->postings[pr->postings_head], doc_id, &size));
  pr->postings_head += size;
  //printf("wrote %u-byte doc_id %u (np1 == %d)\n", size, doc_id, po->num_positions == 1 ? 1 : 0);

  RELAY_ERROR(write_multibyte(&pr->postings[pr->postings_head], offset - po->next_offset, &size));
  pr->postings_head += size;
  //printf("wrote %u-byte offset %u\n", size, offset - po->next_offset);

  if(po->num_positions > 1) {
    RELAY_ERROR(write_multibyte(&pr->postings[pr->postings_head], po->num_positions, &size));
    pr->postings_head += size;
    //printf("wrote %u-byte num positions %u\n", size, po->num_positions);
  }

  for(uint32_t i = 0; i < po->num_positions; i++) {
    RELAY_ERROR(write_multibyte(&pr->postings[pr->postings_head], positions[i] - (i == 0 ? 0 : positions[i - 1]), &size));
    pr->postings_head += size;
    //printf("wrote %u-byte positions %u\n", size, positions[i] - (i == 0 ? 0 : positions[i - 1]));
  }

  //printf("done writing posting\n\n");

  //printf(">>> done writing posting %d %d %d to %p\n\n", (prev_docid == 0 ? po->doc_id : prev_docid - po->doc_id), offset - po->next_offset, po->num_positions, &pr->postings[pl->postings_head]);
  pr->num_postings++;

  return NO_ERROR;
}

/* if include_positions is true, will malloc the positions array for you, and
 * you must free it when done (assuming num_positions > 0)!
 */

wp_error* wp_segment_read_posting(wp_segment* s, uint32_t offset, posting* po, int include_positions) {
  uint32_t size;
  uint32_t orig_offset = offset;
  postings_region* pr = MMAP_OBJ(s->postings, postings_region);

  //DEBUG("reading posting from offset %u -> %p (pr %p base %p)", offset, &pr->postings[offset], pr, &pr->postings);

  RELAY_ERROR(read_multibyte(&pr->postings[offset], &po->doc_id, &size));
  int is_single_posting = po->doc_id & 1;
  po->doc_id = po->doc_id >> 1;
  //DEBUG("read doc_id %u (%u bytes)", po->doc_id, size);
  offset += size;

  RELAY_ERROR(read_multibyte(&pr->postings[offset], &po->next_offset, &size));
  //DEBUG("read next_offset %u -> %u (%u bytes)", po->next_offset, orig_offset - po->next_offset, size);
  if((po->next_offset == 0) || (po->next_offset > orig_offset)) RAISE_ERROR("read invalid next_offset %u (must be > 0 and < %u", po->next_offset, orig_offset);
  po->next_offset = orig_offset - po->next_offset;
  offset += size;

  if(include_positions) {
    if(is_single_posting) po->num_positions = 1;
    else {
      RELAY_ERROR(read_multibyte(&pr->postings[offset], &po->num_positions, &size));
      //DEBUG("read num_positions: %u (%u bytes)", po->num_positions, size);
      offset += size;
    }

    po->positions = malloc(po->num_positions * sizeof(pos_t));

    for(uint32_t i = 0; i < po->num_positions; i++) {
      RELAY_ERROR(read_multibyte(&pr->postings[offset], &po->positions[i], &size));
      offset += size;
      po->positions[i] += (i == 0 ? 0 : po->positions[i - 1]);
      //DEBUG("read position %u (%u bytes)", po->positions[i], size);
    }
  }
  else {
    po->num_positions = 0;
    po->positions = NULL;
  }
  //DEBUG("total record took %u bytes", offset - orig_offset);
  //printf("*** read posting %u %u %u from %u\n", po->doc_id, po->next_offset, po->num_positions, orig_offset);

  return NO_ERROR;
}

wp_error* wp_segment_add_posting(wp_segment* s, const char* field, const char* word, docid_t doc_id, uint32_t num_positions, pos_t positions[]) {
  // TODO move this logic up to ensure_fit()
  int success;

  if(doc_id == 0) RAISE_ERROR("can't add a label to doc 0");

  RELAY_ERROR(bump_stringmap(s, &success));
  RELAY_ERROR(bump_stringpool(s, &success));
  RELAY_ERROR(bump_termhash(s, &success));

  DEBUG("adding posting for %s:%s and doc %u", field, word, doc_id);

  postings_region* pr = MMAP_OBJ(s->postings, postings_region);
  stringmap* sh = MMAP_OBJ(s->stringmap, stringmap);
  termhash* th = MMAP_OBJ(s->termhash, termhash);
  stringpool* sp = MMAP_OBJ(s->stringpool, stringpool);

  // construct the term object
  term t;
  RELAY_ERROR(stringmap_add(sh, sp, field, &t.field_s));
  RELAY_ERROR(stringmap_add(sh, sp, word, &t.word_s));

  // find the offset of the next posting
  posting po;
  uint32_t next_offset = termhash_get_val(th, t);
  if(next_offset == (uint32_t)-1) next_offset = OFFSET_NONE;
  if(next_offset != OFFSET_NONE) { // TODO remove this check for speed once happy
    RELAY_ERROR(wp_segment_read_posting(s, next_offset, &po, 0));
    if(po.doc_id >= doc_id) RAISE_ERROR("cannot add a doc_id out of sorted order");
  }

  // write the entry to the postings region
  uint32_t entry_offset = pr->postings_head;
  //DEBUG("entry will be at offset %u, prev offset is %u and next offset is %u", entry_offset, prev_offset, next_offset);
  po.doc_id = doc_id;
  po.next_offset = next_offset;
  po.num_positions = num_positions;
  RELAY_ERROR(write_posting(s, &po, positions)); // prev_docid is 0 for th
  DEBUG("postings list head now at %u", pr->postings_head);

  // really finally, update the tail pointer so that readers can access this posting
  RELAY_ERROR(termhash_put_val(th, t, entry_offset));

  return NO_ERROR;
}

/*
 * currently, labels are implemented as a separate postings space and separate
 * postings structure, but with the same term hash (the offsets just are
 * relative to the different space).
 *
 * we use the sentinel field value 0 to demarcate a label. since no strings have
 * have stringmap value 0, this is safe.
 *
 * we also maintain a free list of unused label postings. since all label
 * postings are the same size, we can do this to reuse them and avoid losing
 * space in this area; since label postings can be changed frequently, this is
 * desirable. we use the sentinel postings value field=0 word=0 to keep track
 * of this list.
 *
*/
wp_error* wp_segment_read_label(wp_segment* s, uint32_t offset, posting* po) {
  postings_region* pr = MMAP_OBJ(s->labels, postings_region);

  label_posting* lp = wp_segment_label_posting_at(pr, offset);
  po->doc_id = lp->doc_id;
  po->next_offset = lp->next_offset;
  po->num_positions = 0;
  po->positions = NULL;

  return NO_ERROR;
}

wp_error* wp_segment_add_label(wp_segment* s, const char* label, docid_t doc_id) {
  // TODO move this logic up to ensure_fit()
  int success;

  if(doc_id == 0) RAISE_ERROR("can't add a label to doc 0");

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
  uint32_t prev_offset = OFFSET_NONE;
  uint32_t next_offset = termhash_get_val(th, t);
  docid_t last_docid = DOCID_NONE;

  if(next_offset == (uint32_t)-1) next_offset = OFFSET_NONE;
  DEBUG("start offset is %u (none is %u)", next_offset, OFFSET_NONE);

  while(next_offset != OFFSET_NONE) {
    label_posting* lp = wp_segment_label_posting_at(pr, next_offset);

    if((last_docid != DOCID_NONE) && (lp->doc_id >= last_docid)) {
      RAISE_ERROR("whistlepig index corruption! lp %u has docid %u but last docid at lp %u was %u", next_offset, lp->doc_id, prev_offset, last_docid);
    }
    else {
      last_docid = lp->doc_id;
    }

    DEBUG("got doc id %u next_offset %u at offset %u (looking for doc id %u)", lp->doc_id, lp->next_offset, next_offset, doc_id);
    if(lp->doc_id == doc_id) {
      DEBUG("already have label '%s' for doc %u; returning", label, doc_id);
      return NO_ERROR;
    }
    else if(lp->doc_id < doc_id) break;
    prev_offset = next_offset;
    next_offset = lp->next_offset;
  }

  // find a space for the posting by first checking for a free postings in the
  // dead list. the dead list is the list stored under the sentinel term with
  // field 0 and word 0.
  term dead_term = { .field_s = 0, .word_s = 0 };
  uint32_t entry_offset;
  uint32_t dead_offset = termhash_get_val(th, dead_term);
  if(dead_offset == (uint32_t)-1) dead_offset = OFFSET_NONE;

  if(dead_offset == OFFSET_NONE) { // make a new posting
    entry_offset = pr->postings_head;
  }
  else { // we'll use this one; remove it from the linked list
    DEBUG("offset from dead list is %u, using it for the new posting!", dead_offset);
    entry_offset = dead_offset;
    RELAY_ERROR(termhash_put_val(th, dead_term, wp_segment_label_posting_at(pr, dead_offset)->next_offset));
  }

  // finally, write the entry to the label postings region
  DEBUG("label entry will be at offset %u, prev offset is %u and next offset is %u", entry_offset, prev_offset, next_offset);
  label_posting* po = wp_segment_label_posting_at(pr, entry_offset);
  po->doc_id = doc_id;
  po->next_offset = next_offset;

  pr->postings_head += (uint32_t)sizeof(label_posting);
  DEBUG("label postings list head now at %u", pr->postings_head);

  // really finally, update either the previous offset or the tail pointer
  // for this label so that readers can access this posting
  if(prev_offset == OFFSET_NONE) RELAY_ERROR(termhash_put_val(th, t, entry_offset));
  else wp_segment_label_posting_at(pr, prev_offset)->next_offset = entry_offset;

  return NO_ERROR;
}

wp_error* wp_segment_remove_label(wp_segment* s, const char* label, docid_t doc_id) {
  // TODO move this logic to ensure_fit
  int success;
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
  uint32_t prev_offset = OFFSET_NONE;
  uint32_t offset = termhash_get_val(th, t);
  docid_t last_docid = DOCID_NONE;

  if(offset == (uint32_t)-1) offset = OFFSET_NONE;
  label_posting* lp = NULL;

  while(offset != OFFSET_NONE) {
    lp = wp_segment_label_posting_at(pr, offset);

    if((last_docid != DOCID_NONE) && (lp->doc_id >= last_docid)) {
      RAISE_ERROR("whistlepig index corruption! lp %u has docid %u but last docid at lp %u was %u", offset, lp->doc_id, prev_offset, last_docid);
    }
    else {
      last_docid = lp->doc_id;
    }

    if(lp->doc_id < doc_id) offset = OFFSET_NONE; // nasty hack to induce failure
    if(lp->doc_id <= doc_id) break;
    prev_offset = offset;
    offset = lp->next_offset;
  }

  DEBUG("found label posting for doc %u at offset %u; prev_offset is %u", doc_id, offset, prev_offset);

  if(offset == OFFSET_NONE) {
    DEBUG("no label %s found for doc %u", label, doc_id);
    return NO_ERROR;
  }

  // we've found the posting; now remove it from the list
  if(prev_offset == OFFSET_NONE) RELAY_ERROR(termhash_put_val(th, t, lp->next_offset));
  else wp_segment_label_posting_at(pr, prev_offset)->next_offset = lp->next_offset;

  // now add it to the dead list for later reclamation
  term dead_term = { .field_s = 0, .word_s = 0 };
  uint32_t dead_offset = termhash_get_val(th, dead_term);
  if(dead_offset == (uint32_t)-1) dead_offset = OFFSET_NONE;

  lp->next_offset = dead_offset;
  DEBUG("adding dead label posting %u to head of deadlist with next_offset %u", offset, lp->next_offset);
  RELAY_ERROR(termhash_put_val(th, dead_term, offset));

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

  fprintf(stream, "segment has type %u\n", pr->postings_type_and_flags);
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
