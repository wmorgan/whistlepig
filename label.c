#include "whistlepig.h"

/* code for reading to and writing from the label postings region.
 *
 * labels are stored in a separate postings region from text postings, and use
 * a separate postings structure, but share the same term hash (the offsets
 * just are relative to the different space).
 *
 * we use the sentinel field value 0 to demarcate a label. since no strings
 * have have stringmap value 0, this is safe.
 *
 * we also maintain a free list of unused label postings. since all label
 * postings are the same size, we can do this to reuse them and avoid losing
 * space in this area; since label postings can be changed frequently, this is
 * desirable. we use the sentinel postings value field=0 word=0 to keep track
 * of this list.
 *
 */
#define LABEL_POSTINGS_REGION_TYPE_DEFAULT 1

#define wp_segment_label_posting_at(label_postings_region, offset) ((label_posting*)(label_postings_region->postings + offset))

static postings_list_header blank_plh = { .count = 0, .next_offset = OFFSET_NONE };
static term dead_term = { .field_s = 0, .word_s = 0 }; // index of the dead list for reclaiming labels

wp_error* label_postings_region_init(label_postings_region* pr, uint32_t initial_size) {
  pr->postings_type_and_flags = LABEL_POSTINGS_REGION_TYPE_DEFAULT;
  pr->num_postings = 0;
  pr->postings_head = 1; // skip one byte, which is reserved as OFFSET_NONE
  pr->postings_tail = initial_size;

  return NO_ERROR;
}

wp_error* label_postings_region_validate(label_postings_region* pr) {
  if(pr->postings_type_and_flags != LABEL_POSTINGS_REGION_TYPE_DEFAULT) RAISE_ERROR("postings region has type %u; expecting type %u", pr->postings_type_and_flags, LABEL_POSTINGS_REGION_TYPE_DEFAULT);
  return NO_ERROR;
}

wp_error* label_postings_region_ensure_fit(mmap_obj* mmopr, uint32_t postings_bytes, int* success) {
  label_postings_region* pr = MMAP_OBJ_PTR(mmopr, label_postings_region);
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
    pr = MMAP_OBJ_PTR(mmopr, label_postings_region); // may have changed!
    pr->postings_tail = new_tail;
  }

  *success = 1;
  return NO_ERROR;
}

wp_error* label_postings_region_read_label(label_postings_region* pr, uint32_t offset, label_posting* po) {
  label_posting* lp = wp_segment_label_posting_at(pr, offset);
  memcpy(po, lp, sizeof(label_posting));

  return NO_ERROR;
}

wp_error* label_postings_add_label(wp_segment* s, const char* label, docid_t doc_id) {
  if(doc_id == 0) RAISE_ERROR("can't add a label to doc 0");

/*
  int success;

  // TODO move this logic up to ensure_fit()
  RELAY_ERROR(bump_stringmap(s, &success));
  RELAY_ERROR(bump_stringpool(s, &success));
  RELAY_ERROR(bump_termhash(s, &success));
*/

  DEBUG("adding label '%s' to doc %u", label, doc_id);

  label_postings_region* pr = MMAP_OBJ(s->labels, label_postings_region);
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

  uint32_t next_offset = plh->next_offset;
  docid_t last_docid = DOCID_NONE;
  uint32_t prev_offset = OFFSET_NONE;

  DEBUG("start offset is %u (none is %u)", next_offset, OFFSET_NONE);

  while(next_offset != OFFSET_NONE) {
    label_posting* lp = wp_segment_label_posting_at(pr, next_offset);

    if((last_docid != DOCID_NONE) && (lp->doc_id >= last_docid))
      RAISE_ERROR("whistlepig index corruption! lp %u has docid %u but last docid at lp %u was %u", next_offset, lp->doc_id, prev_offset, last_docid);
    else
      last_docid = lp->doc_id;

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
  postings_list_header* dead_plh = termhash_get_val(th, dead_term);
  if(dead_plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, dead_term, &blank_plh));
    dead_plh = termhash_get_val(th, t);
  }

  uint32_t entry_offset;
  uint32_t dead_offset = dead_plh->next_offset;

  if(dead_offset == OFFSET_NONE) { // make a new posting
    entry_offset = pr->postings_head;
  }
  else { // we'll use this one; remove it from the linked list
    DEBUG("offset from dead list is %u, using it for the new posting!", dead_offset);
    entry_offset = dead_plh->next_offset;
    dead_plh->next_offset = wp_segment_label_posting_at(pr, dead_offset)->next_offset;
    dead_plh->count--;
  }

  // finally, write the entry to the label postings region
  DEBUG("label entry will be at offset %u, prev offset is %u and next offset is %u", entry_offset, prev_offset, next_offset);
  label_posting* po = wp_segment_label_posting_at(pr, entry_offset);
  po->doc_id = doc_id;
  po->next_offset = next_offset;

  pr->postings_head += (uint32_t)sizeof(label_posting);
  DEBUG("label posting list head now at %u", pr->postings_head);

  // really finally, update either the previous offset or the tail pointer
  // for this label so that readers can access this posting
  plh->count++;
  if(prev_offset == OFFSET_NONE) plh->next_offset = entry_offset;
  else wp_segment_label_posting_at(pr, prev_offset)->next_offset = entry_offset;

  return NO_ERROR;
}

wp_error* wp_segment_remove_label(wp_segment* s, const char* label, docid_t doc_id) {
/*
  // TODO move this logic to ensure_fit
  int success;

  RELAY_ERROR(bump_termhash(s, &success)); // we might add an entry for the dead list
  */

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
  docid_t last_docid = DOCID_NONE;
  uint32_t prev_offset = OFFSET_NONE;
  postings_list_header* plh = termhash_get_val(th, t);
  if(plh == NULL) {
    DEBUG("no such label %s", label);
    return NO_ERROR;
  }

  uint32_t offset = plh->next_offset;
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
  if(prev_offset == OFFSET_NONE) plh->next_offset = lp->next_offset;
  else wp_segment_label_posting_at(pr, prev_offset)->next_offset = lp->next_offset;
  plh->count--;

  // now add it to the dead list for later reclamation
  postings_list_header* dead_plh = termhash_get_val(th, dead_term);
  if(dead_plh == NULL) {
    RELAY_ERROR(termhash_put_val(th, dead_term, &blank_plh));
    dead_plh = termhash_get_val(th, t);
  }

  DEBUG("adding dead label posting %u to head of deadlist with next_offset %u", offset, lp->next_offset);

  uint32_t dead_offset = dead_plh->next_offset;
  lp->next_offset = dead_offset;
  dead_plh->next_offset = offset;

  return NO_ERROR;
}
