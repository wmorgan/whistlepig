#include "whistlepig.h"
#include "posting.h"

#define POSTINGS_REGION_TYPE_DEFAULT 1

#define wp_segment_label_posting_at(posting_region, offset) ((label_posting*)(posting_region->postings + offset))

static postings_list_header blank_plh = { .count = 0, .next_offset = OFFSET_NONE };
static term dead_term = { .field_s = 0, .word_s = 0 };

wp_error* postings_region_init(postings_region* pr, uint32_t initial_size) {
  pr->postings_type_and_flags = POSTINGS_REGION_TYPE_DEFAULT;
  pr->num_postings = 0;
  pr->postings_head = 1; // skip one byte, which is reserved as OFFSET_NONE
  pr->postings_tail = initial_size;
}

wp_error* postings_region_validate(postings_region* pr) {
  if(pr->postings_type_and_flags != POSTINGS_REGION_TYPE_IMMUTABLE_VBE) RAISE_ERROR("postings region has type %u; expecting type %u", pr->postings_type_and_flags, POSTINGS_REGION_TYPE_DEFAULT);
  return NO_ERROR;
}

// TODO share code with label_posting_region
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

#define VALUE_BITMASK 0x7f

static uint32_t sizeof_uint32_vbe(uint32_t val) {
  uint32_t size = 1;

  while(val > VALUE_BITMASK) {
    val >>= 7;
    size++;
  }

  return size;
}

RAISING_STATIC(write_uint32_vbe(uint8_t* location, uint32_t val, uint32_t* size)) {
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

RAISING_STATIC(read_uint32_vbe(uint8_t* location, uint32_t* val, uint32_t* size)) {
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

// calculate the size of a posting
static uint32_t sizeof_posting(posting* po) {
  uint32_t size = 0;

  uint32_t doc_id = po->doc_id << 1;
  if(po->num_positions == 1) doc_id |= 1; // marker for single postings
  size += sizeof_uint32_vbe(po->doc_id);

  if(po->num_positions > 1) size += sizeof_uint32_vbe(po->num_positions);
  for(uint32_t i = 0; i < po->num_positions; i++) size += sizeof_uint32_vbe(po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]));

  return size;
}

// write an encoded posting to a region of memory
RAISING_STATIC(write_posting(posting* po, uint8_t* target, uint32_t* total_size)) {
  uint32_t size;
  uint8_t* start = target;

  uint32_t doc_id = po->doc_id << 1;
  if(po->num_positions == 1) doc_id |= 1; // marker for single postings
  RELAY_ERROR(write_uint32_vbe(target, doc_id, &size));
  target += size;
  //printf("wrote %u-byte doc_id %u (np1 == %d)\n", size, doc_id, po->num_positions == 1 ? 1 : 0);

  if(po->num_positions > 1) {
    RELAY_ERROR(write_uint32_vbe(target, po->num_positions, &size));
    target += size;
    //printf("wrote %u-byte num positions %u\n", size, po->num_positions);
  }

  for(uint32_t i = 0; i < po->num_positions; i++) {
    RELAY_ERROR(write_uint32_vbe(target, po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]), &size));
    target += size;
    //printf("wrote %u-byte positions %u\n", size, positions[i] - (i == 0 ? 0 : positions[i - 1]));
  }

  *total_size = (target - start);

  //printf("done writing posting\n\n");

  //printf(">>> done writing posting %d %d %d to %p\n\n", (prev_docid == 0 ? po->doc_id : prev_docid - po->doc_id), offset - po->next_offset, po->num_positions, &pr->postings[pl->postings_head]);

  return NO_ERROR;
}

// add a posting to a postings_block
RAISING_STATIC(add_posting_to_block(postings_block* block, posting* po)) {
  // basic sanity checks
  if(po->num_positions == 0) RAISE_ERROR("num_positions == 0");
  if(po->positions == NULL) RAISE_ERROR("positions is null");
  if(po->doc_id <= block->max_docid) RAISE_ERROR("doc_id is <= current max_docid"); // should always be adding larger docids

  // calculate size of posting + rewritten docid.
  // format will be:
  // - cur_docid (encoded as 0 since it's delta max_docid, which will be updated to be this value)
  // - if # positions > 1
  // -   # positions
  // -   positions, delta-encoded
  // - old cur_docid, re-encoded as delta cur_docid (currently 0, delta the old max_docid)

  uint32_t cur_posting_size = sizeof_posting(po); // gonna write this
  uint32_t prev_docid_size = sizeof_uint32_vbe(po->doc_id - block->max_docid); // gonna rewrite this from 0
  uint32_t total_size = cur_posting_size + prev_docid_size - 1; // minus 1 for the single-byte docids that's to be rewritten
  uint32_t remaining_bytes = block->postings_head; // easy enough

  if(remaining_bytes < total_size) RAISE_ERROR("not enough space to write posting");

  // now write everything
  uint8_t* start = &block->data[block->postings_head - total_size];
  uint32_t size;
  RELAY_ERROR(write_posting(po, start, &size));
  start += size;
  RELAY_ERROR(write_uint32_vbe(start, po->doc_id - block->max_docid, &size));

  // update max_docid for this block
  block->max_docid = po->doc_id;

  // and we're done
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

  RELAY_ERROR(read_uint32_vbe(&pr->postings[offset], &po->doc_id, &size));
  int is_single_posting = po->doc_id & 1;
  po->doc_id = po->doc_id >> 1;
  //DEBUG("read doc_id %u (%u bytes)", po->doc_id, size);
  offset += size;

  RELAY_ERROR(read_uint32_vbe(&pr->postings[offset], &po->next_offset, &size));
  //DEBUG("read next_offset %u -> %u (%u bytes)", po->next_offset, orig_offset - po->next_offset, size);
  if((po->next_offset == 0) || (po->next_offset > orig_offset)) RAISE_ERROR("read invalid next_offset %u (must be > 0 and < %u)", po->next_offset, orig_offset);
  po->next_offset = orig_offset - po->next_offset;
  offset += size;

  if(include_positions) {
    if(is_single_posting) po->num_positions = 1;
    else {
      RELAY_ERROR(read_uint32_vbe(&pr->postings[offset], &po->num_positions, &size));
      //DEBUG("read num_positions: %u (%u bytes)", po->num_positions, size);
      offset += size;
    }

    po->positions = malloc(po->num_positions * sizeof(pos_t));

    for(uint32_t i = 0; i < po->num_positions; i++) {
      RELAY_ERROR(read_uint32_vbe(&pr->postings[offset], &po->positions[i], &size));
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

  // find the offset of the next posting
  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, t, &blank_plh));
    plh = termhash_get_val(th, t);
  }
  DEBUG("posting list header for %s:%s is at %p", field, word, plh);

  posting po;
  uint32_t next_offset = plh->next_offset;

  if(next_offset != OFFSET_NONE) { // TODO remove this check for speed once happy [PERFORMANCE]
    RELAY_ERROR(wp_segment_read_posting(s, next_offset, &po, 0));
    if(po.doc_id >= doc_id) RAISE_ERROR("cannot add a doc_id out of sorted order");
  }

  // write the entry to the postings region
  uint32_t entry_offset = pr->postings_head;
  DEBUG("writing posting at offset %u. next offset is %u.", entry_offset, next_offset);

  po.doc_id = doc_id;
  po.next_offset = next_offset;
  po.num_positions = num_positions;
  po.positions = positions;
  RELAY_ERROR(write_posting_to_block(s, &po, positions)); // prev_docid is 0 for th
  DEBUG("posting list head now at %u", pr->postings_head);

  // really finally, update the tail pointer so that readers can access this posting
  plh->count++;
  plh->next_offset = entry_offset;
  DEBUG("posting list header for %s:%s now reads count=%u offset=%u", field, word, plh->count, plh->next_offset);

  return NO_ERROR;
}
