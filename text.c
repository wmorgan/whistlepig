#include "text.h"

wp_error* wp_text_postings_region_init(postings_region* pr, uint32_t initial_size) {
  RELAY_ERROR(wp_postings_region_init(pr, initial_size, POSTINGS_REGION_TYPE_IMMUTABLE_VBE_BLOCKS));
  return NO_ERROR;
}

wp_error* wp_text_postings_region_validate(postings_region* pr) {
  RELAY_ERROR(wp_postings_region_validate(pr, POSTINGS_REGION_TYPE_IMMUTABLE_VBE_BLOCKS));
  return NO_ERROR;
}

#define VALUE_BITMASK 0x7f

static inline uint32_t sizeof_uint32_vbe(uint32_t val) {
  uint32_t size = 1;

  while(val > VALUE_BITMASK) {
    val >>= 7;
    size++;
  }

  return size;
}

RAISING_STATIC(write_uint32_vbe(uint8_t* location, uint32_t val, uint32_t* size)) {
  //printf("    writing %u to position %p as:\n", val, location);
  uint8_t* start = location;

  while(val > VALUE_BITMASK) {
    uint8_t c = (val & VALUE_BITMASK) | 0x80;
    *location = c;
    //printf("      %d = %d | %d at %p\n", c, val & VALUE_BITMASK, 0x80, location);
    location++;
    val >>= 7;
  }
  uint8_t c = (val & VALUE_BITMASK);
  *location = c;
  //printf("      %d at %p\n", c, location);
  *size = (uint32_t)(location + 1 - start);
  //printf("    total %u bytes\n", *size);
  return NO_ERROR;
}

RAISING_STATIC(read_uint32_vbe(uint8_t* location, uint32_t* val, uint32_t* size)) {
  uint8_t* start = location;
  uint32_t shift = 0;

  //printf("    reading from position %p\n", location);
  *val = 0;
  while(*location & 0x80) {
    //printf("       read continuation byte %d -> %d at %p\n", *location, *location & ~0x80, location);
    *val |= (*location & ~0x80) << shift;
    shift += 7;
    location++;
  }
  *val |= *location << shift;
  //printf("      read final byte %d at %p\n", *location, location);
  *size = (uint32_t)(location + 1 - start);
  //printf("    read %d bytes total, val = %d\n", *size, *val);
  return NO_ERROR;
}

static inline uint32_t sizeof_poslist(posting* po) {
  uint32_t size = 0;
  for(uint32_t i = 0; i < po->num_positions; i++) size += sizeof_uint32_vbe(po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]));
  return size;
}

// calculate the size of a posting
static inline uint32_t sizeof_posting(posting* po) {
  uint32_t size = 0;
  uint32_t doc_id = po->doc_id << 1;
  uint32_t poslist_size = sizeof_poslist(po);

  if(poslist_size == 1) doc_id |= 1; // marker for single-byte-position-list postings
  else size = sizeof_uint32_vbe(poslist_size);

  return size + poslist_size + sizeof_uint32_vbe(doc_id);
}

// write an encoded posting to a region of memory
RAISING_STATIC(write_posting(posting* po, uint8_t* target, uint32_t* total_size)) {
  uint32_t size;
  uint8_t* start = target;

  //printf("  writing posting %u with %u positions to %p\n", po->doc_id, po->num_positions, start);

  uint32_t poslist_size = sizeof_poslist(po);
  uint32_t doc_id = po->doc_id << 1;
  if(poslist_size == 1) doc_id |= 1; // marker for single-byte-poslist postings

  //printf("  writing doc_id %u encoded as %u\n", po->doc_id, doc_id);
  RELAY_ERROR(write_uint32_vbe(target, doc_id, &size));
  target += size;
  //printf("  wrote %u-byte doc_id %u\n", size, doc_id);

  if(poslist_size > 1) {
    //printf("  writing poslist_size %u\n", poslist_size);
    RELAY_ERROR(write_uint32_vbe(target, poslist_size, &size));
    target += size;
    //printf("  wrote %u-byte postlist_size %u\n", size, poslist_size);
  }

  for(uint32_t i = 0; i < po->num_positions; i++) {
    //printf("  writing position %u encoded as %u\n", po->positions[i], po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]));
    RELAY_ERROR(write_uint32_vbe(target, po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]), &size));
    target += size;
    //printf("  wrote %u-byte positions %u\n", size, po->positions[i] - (i == 0 ? 0 : po->positions[i - 1]));
  }

  *total_size = (target - start);

  //printf("  done writing %u-byte posting %u with %u positions to %p\n", *total_size, po->doc_id, po->num_positions, start);

  return NO_ERROR;
}

// add a posting to a postings_block
RAISING_STATIC(add_posting_to_block(postings_block* block, posting* po)) {
  DEBUG("going to add a posting for doc %u with %u positions", po->doc_id, po->num_positions);
  DEBUG("block has min_docid %u and max_docid %u", block->min_docid, block->max_docid);

  // basic sanity checks
  if(po->num_positions == 0) RAISE_ERROR("num_positions == 0");
  if(po->positions == NULL) RAISE_ERROR("positions is null");

  // we're going to replace the doc_id with 0 for the rest of this
  // function, since that's what we'll actually be writing.
  posting zeroed_posting = *po;
  zeroed_posting.doc_id = 0;

  uint32_t cur_posting_size = sizeof_posting(&zeroed_posting);
  DEBUG("posting is %u bytes itself. block has %u bytes remaining.", cur_posting_size, block->postings_head);

  if(block->max_docid == 0) {
    DEBUG("max_docid is 0, so this is the first posting in this block");
    if(block->postings_head < cur_posting_size) RAISE_RESIZE_ERROR(RESIZE_ERROR_POSTINGS_BLOCK, cur_posting_size);

    block->postings_head -= cur_posting_size;
    DEBUG("rewound postings head by %u bytes", cur_posting_size);

    block->max_docid = block->min_docid = po->doc_id; // the original doc_id

    uint32_t size;
    RELAY_ERROR(write_posting(&zeroed_posting, &block->data[block->postings_head], &size));
    if(size != cur_posting_size) RAISE_ERROR("size mismatch: %u vs %u", size, cur_posting_size); // sanity check
  }
  else {
    DEBUG("going to add an additional posting to this block");

    if(po->doc_id <= block->max_docid) RAISE_ERROR("doc_id is <= current max_docid"); // should always be adding larger docids

    // calculate size of posting + rewritten docid.
    // format will be:
    // - cur_docid (encoded as 0 since it's delta max_docid, which will be updated to be this value)
    // - if poslist_size > 1, the poslist_size
    // - the positions, delta-encoded
    // - old cur_docid, re-encoded as delta cur_docid (currently 0, delta the old max_docid)

    // a little extra logic to handle the single-poslist-byte trick
    int prev_docid_is_single = block->data[block->postings_head] & 1;
    DEBUG("prev docid is single: %u", prev_docid_is_single);

    uint32_t prev_docid_delta_encoded = po->doc_id - block->max_docid;
    prev_docid_delta_encoded <<= 1;
    prev_docid_delta_encoded |= prev_docid_is_single;
    DEBUG("prev docid will be represented as %u", prev_docid_delta_encoded);

    uint32_t prev_docid_size = sizeof_uint32_vbe(prev_docid_delta_encoded); // gonna rewrite this from 0
    uint32_t total_size = cur_posting_size + prev_docid_size - 1; // minus 1 for the single-byte docids that's going to be rewritten
    DEBUG("posting is %u bytes. rewrite of prev docid is %u bytes. total requirement is %u bytes", cur_posting_size, prev_docid_size, total_size);
    if(block->postings_head < total_size) RAISE_RESIZE_ERROR(RESIZE_ERROR_POSTINGS_BLOCK, total_size);

    // now write everything
    DEBUG("rewinding postings head from %u to %u", block->postings_head, block->postings_head - total_size);
    block->postings_head -= total_size;

    uint32_t size;
    DEBUG("writing new posting to %u", block->postings_head);
    RELAY_ERROR(write_posting(&zeroed_posting, &block->data[block->postings_head], &size));
    if(size != cur_posting_size) RAISE_ERROR("size mismatch");

    DEBUG("overwriting old posting at %u", block->postings_head + size);
    RELAY_ERROR(write_uint32_vbe(&block->data[block->postings_head + size], prev_docid_delta_encoded, &size));
    if(size != prev_docid_size) RAISE_ERROR("size mismatch");

    block->max_docid = po->doc_id;
    DEBUG("setting block max_docid to %u", po->doc_id);
  }

  // and we're done
  return NO_ERROR;
}

#define MIN_BLOCK_SIZE 32
#define SOFT_MAX_BLOCK_SIZE 512 // this seems to work the best -- but tweak me!
#define HARD_MAX_BLOCK_SIZE ((64 * 1024) - sizeof(postings_block)) // can never be exceeded
RAISING_STATIC(build_new_block(postings_region* pr, uint32_t min_size, uint32_t old_offset, uint32_t* new_offset)) {
  DEBUG("going to make a new block to hold %u bytes", min_size);
  if(min_size < 1) RAISE_ERROR("min_size is %d", min_size); // sanity check

  uint32_t new_size = MIN_BLOCK_SIZE;

  // bump up size to be greater than the previous block, if any
  if(old_offset != OFFSET_NONE) {
    postings_block* old_block = wp_postings_block_at(pr, old_offset);
    DEBUG("previous block for this posting is %u bytes", old_block->size);
    new_size = old_block->size * 2;
  }

  while(new_size < min_size) new_size *= 2;
  if(new_size > SOFT_MAX_BLOCK_SIZE) new_size = SOFT_MAX_BLOCK_SIZE;
  if(new_size < min_size) new_size = min_size;
  if(new_size > HARD_MAX_BLOCK_SIZE) RAISE_ERROR("posting is too big to be represented: %d => %d", min_size, new_size);

  DEBUG("going to make a new block of %u + %zu = %zu bytes", new_size, sizeof(postings_block), new_size + sizeof(postings_block));
  new_size += sizeof(postings_block);

  DEBUG("have %u bytes left in this postings region", pr->postings_tail - pr->postings_head);

  if((pr->postings_tail - pr->postings_head) < new_size) {
    DEBUG("*** only have %u bytes in this postings region left. unwinding so that we can resize and retry!", pr->postings_tail - pr->postings_head);
    RAISE_RESIZE_ERROR(RESIZE_ERROR_POSTINGS_REGION, new_size);
  }

  *new_offset = pr->postings_head;
  pr->postings_head += new_size;

  DEBUG("new block is at offset %u and will have %u bytes for postings", *new_offset, new_size - sizeof(postings_block));
  postings_block* block = wp_postings_block_at(pr, *new_offset);
  block->prev_block_offset = old_offset;
  block->size = new_size - sizeof(postings_block);
  block->postings_head = block->size;
  block->min_docid = 0;
  block->max_docid = 0;

  return NO_ERROR;
}

// add the posting to a block, adding a block as necessary,
wp_error* wp_text_postings_region_add_posting(postings_region* pr, posting* po, struct postings_list_header* plh) {
  if(po->doc_id == 0) RAISE_ERROR("can't add doc 0");

  uint32_t posting_size = sizeof_posting(po);
  DEBUG("want to add posting of %u bytes", posting_size);

  // first, let's get a block to hold this posting
  postings_block* block = NULL;

  // do we have one we can use already?
  if(plh->next_offset == OFFSET_NONE) {
    DEBUG("no block for this postings list. going to make a new one!");
    RELAY_ERROR(build_new_block(pr, posting_size, plh->next_offset, &plh->next_offset));
  }
  else {
    // sanity check
    if((plh->next_offset > pr->postings_head) || (plh->next_offset < 1)) RAISE_ERROR("plh offset out of bounds: %u not in (1, %u)", plh->next_offset, pr->postings_head);
  }

  // we have a block, so let's add the posting!
  wp_error* e = NULL;
retry:
  block = wp_postings_block_at(pr, plh->next_offset);
  DEBUG("postings size is %u and we have %u bytes left in block. let's go!", posting_size, block->postings_head);

  e = add_posting_to_block(block, po);
  if(e != NULL) {
    if(e->type == WP_ERROR_TYPE_RESIZE) {
      wp_resize_error_data* data = (wp_resize_error_data*)e->data;
      DEBUG("caught block resize signal. allocating a new block...");
      RELAY_ERROR(build_new_block(pr, data->size, plh->next_offset, &plh->next_offset));
      wp_error_free(e);

      DEBUG("caught block resize signal. retrying...");
      goto retry;
    }
    else RELAY_ERROR(e);
  }

  // congrats, a new posting
  plh->count++;

  return NO_ERROR;
}

/* if include_positions is true, will malloc the positions array for you, and
 * you must free it when done (assuming num_positions > 0)!
 *
 * will update next_offset to the offset of the next posting. if this is
 * >= block->size, you're done with this block and should move on to the next one.
 *
 * note that docids will be returned in delta form, so you must subtract them
 * from the previous docid returned from this block to get the actual docid!
 * if this is the first posting in the block, subtract from max_id (the
 * delta should be 0).
 */
wp_error* wp_text_postings_region_read_posting_from_block(postings_region* pr, postings_block* block, uint32_t offset, uint32_t* next_offset, posting* po, int include_positions) {
  (void)pr;
  uint32_t size;
  //uint32_t orig_offset = offset;

  DEBUG("reading posting from offset %u -> %p (pr %p base %p)", offset, &pr->postings[offset], pr, &pr->postings);

  if(offset < block->postings_head) RAISE_ERROR("too-small offset: %u vs %u", offset, block->postings_head);
  if(offset >= block->size) RAISE_ERROR("too-large offset: %u vs %u", offset, block->size);

  RELAY_ERROR(read_uint32_vbe(&block->data[offset], &po->doc_id, &size));
  int is_single_posting = po->doc_id & 1;
  po->doc_id = po->doc_id >> 1;
  DEBUG("read doc_id %u (%u bytes)", po->doc_id, size);
  offset += size;

  // get size of position list
  uint32_t poslist_size = 0;
  if(is_single_posting) poslist_size = 1;
  else {
    RELAY_ERROR(read_uint32_vbe(&block->data[offset], &poslist_size, &size));
    DEBUG("read poslist_size: %u (%u bytes)", poslist_size, size);
    offset += size;
  }

  if(include_positions) { // read the position list in
    // the number of bytes in the position list is the upper bound on the number
    // of positions, so we'll allocate that many.
    po->positions = malloc(poslist_size * sizeof(pos_t));
    po->num_positions = 0;

    // now let's read them in
    while(poslist_size > 0) {
      RELAY_ERROR(read_uint32_vbe(&block->data[offset], &po->positions[po->num_positions], &size));
      offset += size;
      po->positions[po->num_positions] += (po->num_positions == 0 ? 0 : po->positions[po->num_positions - 1]);
      DEBUG("read position %u (%u bytes)", po->positions[po->num_positions], size);
      po->num_positions++;
      poslist_size -= size;
    }
  }
  else {
    po->num_positions = 0;
    po->positions = NULL;
    offset += poslist_size;
  }

  *next_offset = offset;
  //DEBUG("total record took %u bytes", offset - orig_offset);
  //printf("*** read posting %u %u from %u\n", po->doc_id, po->num_positions, orig_offset);

  return NO_ERROR;
}
