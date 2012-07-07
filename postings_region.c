#include "postings_region.h"

wp_error* wp_postings_region_init(postings_region* pr, uint32_t initial_size, uint32_t type_and_flags) {
  pr->postings_type_and_flags = type_and_flags;
  pr->num_postings = 0;
  pr->postings_head = 1; // skip one byte, which is reserved as OFFSET_NONE
  pr->postings_tail = initial_size;

  return NO_ERROR;
}

wp_error* wp_postings_region_validate(postings_region* pr, uint32_t type_and_flags) {
  if(pr->postings_type_and_flags != type_and_flags) RAISE_ERROR("postings region has type %u; expecting type %u", pr->postings_type_and_flags, type_and_flags);
  return NO_ERROR;
}

wp_error* wp_postings_region_ensure_fit(mmap_obj* mmopr, uint32_t new_size, int* success) {
  postings_region* pr = MMAP_OBJ_PTR(mmopr, postings_region);

  DEBUG("ensuring fit for %u postings bytes", new_size);

  uint32_t new_head = pr->postings_head + new_size;
  uint32_t new_tail = pr->postings_tail;
  while(new_tail <= new_head) new_tail = new_tail * 2;

  if(new_tail > MAX_POSTINGS_REGION_SIZE - sizeof(mmap_obj_header)) new_tail = MAX_POSTINGS_REGION_SIZE - sizeof(mmap_obj_header);
  DEBUG("new tail will be %u, current is %u, max is %u", new_tail, pr->postings_tail, MAX_POSTINGS_REGION_SIZE);

  if(new_tail <= new_head) { // can't increase enough
    *success = 0;
    return NO_ERROR;
  }

  if(new_tail != pr->postings_tail) { // need to resize
    DEBUG("request for %u postings bytes, old tail is %u, new tail will be %u, max is %u\n", new_size, pr->postings_tail, new_tail, MAX_POSTINGS_REGION_SIZE);
    RELAY_ERROR(mmap_obj_resize(mmopr, new_tail));
    pr = MMAP_OBJ_PTR(mmopr, postings_region); // may have changed!
    pr->postings_tail = new_tail;
  }

  *success = 1;
  return NO_ERROR;
}

