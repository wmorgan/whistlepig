#include "whistlepig.h"

static const int HASH_PRIME_SIZE = 32;

static const uint32_t prime_list[] = {
  0ul,          3ul,          11ul,         23ul,         53ul,
  97ul,         193ul,        389ul,        769ul,        1543ul,
  3079ul,       6151ul,       12289ul,      24593ul,      49157ul,
  98317ul,      196613ul,     393241ul,     786433ul,     1572869ul,
  3145739ul,    6291469ul,    12582917ul,   25165843ul,   50331653ul,
  100663319ul,  201326611ul,  402653189ul,  805306457ul,  1610612741ul,
  3221225473ul, 4294967291ul
};

#define isempty(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&2)
#define isdel(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&1)
#define iseither(flag, i) ((flag[i>>4]>>((i&0xfU)<<1))&3)
#define set_isdel_false(flag, i) (flag[i>>4]&=~(1ul<<((i&0xfU)<<1)))
#define set_isempty_false(flag, i) (flag[i>>4]&=~(2ul<<((i&0xfU)<<1)))
#define set_isboth_false(flag, i) (flag[i>>4]&=~(3ul<<((i&0xfU)<<1)))
#define set_isdel_true(flag, i) (flag[i>>4]|=1ul<<((i&0xfU)<<1))

static const double HASH_UPPER = 0.77;

static uint32_t hash_term(term t) {
  return t.word_s ^ t.field_s;
}

static int term_equals(term a, term b) {
  return a.word_s == b.word_s && a.field_s == b.field_s;
}

void termhash_init(termhash* h) {
  h->n_buckets_idx = 1;
  h->n_buckets = prime_list[h->n_buckets_idx];
  h->upper_bound = (uint32_t)(h->n_buckets * HASH_UPPER + 0.5);
  h->size = h->n_occupied = 0;
  termhash_setup(h);
  memset(h->flags, 0xaa, ((h->n_buckets>>4) + 1) * sizeof(uint32_t));
}

#define OFFSET(a, b) (long)((uint8_t*)a - (uint8_t*)b)
// set flags, keys and vals to correct locations based on h->n_buckets
void termhash_setup(termhash* h) {
  DEBUG("term hash ranges from %p to %p (size %u)", h, (char*)h + termhash_size(h), termhash_size(h));
  DEBUG("boundary is at %p (+%ld)", h->boundary, OFFSET(h->boundary, h));
  h->flags = (uint32_t*)h->boundary;
  h->keys = (term*)((uint32_t*)h->boundary + ((h->n_buckets >> 4) + 1));
  h->vals = (uint32_t*)((term*)h->keys + h->n_buckets);
  DEBUG("flags are at %p (+%ld)", h->flags, OFFSET(h->flags, h->boundary));
  DEBUG(" keys are at %p (+%ld)", h->keys, OFFSET(h->keys, h->boundary));
  DEBUG(" vals are at %p (+%ld)", h->vals, OFFSET(h->vals, h->boundary));
}

/*
static void termhash_dump(termhash* h) {
  for(uint32_t i = 0; i < h->n_buckets; i++) {
    if(isempty(h->flags, i)) printf("%u:\n", i);
    else if(isdel(h->flags, i)) printf("%u: [deleted]", i);
    else {
      term t = h->keys[i];
      printf("%u: (%u,%u)\n", i, t.field_s, t.word_s);
    }
  }
}
*/

/*
static void kh_destroy_##name(kh_##name##_t *h) {
  if (h) {
    free(h->keys); free(h->flags);
    free(h->vals);
    free(h);
  }
}

static void kh_clear_##name(kh_##name##_t *h) {
  if (h && h->flags) {
    memset(h->flags, 0xaa, ((h->n_buckets>>4) + 1) * sizeof(uint32_t));
    h->size = h->n_occupied = 0;
  }
}
*/

uint32_t termhash_get(termhash *h, term key) {
  if(h->n_buckets) {
    uint32_t inc, k, i, last;
    k = hash_term(key); i = k % h->n_buckets;
    inc = 1 + k % (h->n_buckets - 1); last = i;
    while (!isempty(h->flags, i) && (isdel(h->flags, i) || !term_equals(h->keys[i], key))) {
      if (i + inc >= h->n_buckets) i = i + inc - h->n_buckets;
      else i += inc;
      if (i == last) return h->n_buckets;
    }
    return iseither(h->flags, i)? h->n_buckets : i;
  }
  else return 0;
}

wp_error* termhash_bump_size(termhash *h) {
  DEBUG("bumping size for term hash at %p with size %u and boundary %p (+%ld)", h, termhash_size(h), h->boundary, (long)((uint8_t*)h->boundary - (uint8_t*)h));
  DEBUG("flags are at %p (+%ld)", h->flags, OFFSET(h->flags, h->boundary));
  DEBUG(" keys are at %p (+%ld)", h->keys, OFFSET(h->keys, h->boundary));
  DEBUG(" vals are at %p (+%ld)", h->vals, OFFSET(h->vals, h->boundary));

  h->n_buckets_idx++;
  if(h->n_buckets_idx > HASH_PRIME_SIZE) exit(1); // die horribly TODO fixme
  uint32_t new_n_buckets = prime_list[h->n_buckets_idx];

  // first make a backup of the oldflags
  size_t oldflagsize = ((h->n_buckets >> 4) + 1) * sizeof(uint32_t);
  uint32_t* oldflags = malloc(oldflagsize);
  memcpy(oldflags, h->flags, oldflagsize);

  // keep pointers to the old locations
  term* oldkeys = h->keys;
  uint32_t* oldvals = h->vals;

  // set pointers to the new locations
  h->keys = (term*)((uint32_t*)h->boundary + ((new_n_buckets >> 4) + 1));
  h->vals = (uint32_t*)((term*)h->keys + new_n_buckets);

  // move the vals and keys
  memmove(h->vals, oldvals, h->n_buckets * sizeof(uint32_t));
  memmove(h->keys, oldkeys, h->n_buckets * sizeof(term));

  // clear the new flags
  memset(h->flags, 0xaa, ((new_n_buckets>>4) + 1) * sizeof(uint32_t));

  // do the complicated stuff from khash.h
  for (unsigned int j = 0; j != h->n_buckets; ++j) {
    if (iseither(oldflags, j) == 0) {
      term key = h->keys[j];
      uint32_t val;
      val = h->vals[j];
      set_isdel_true(oldflags, j);
      while (1) {
        uint32_t inc, k, i;
        k = hash_term(key);
        i = k % new_n_buckets;
        inc = 1 + k % (new_n_buckets - 1);
        while (!isempty(h->flags, i)) {
          if (i + inc >= new_n_buckets) i = i + inc - new_n_buckets;
          else i += inc;
        }
        set_isempty_false(h->flags, i);
        if (i < h->n_buckets && iseither(oldflags, i) == 0) {
          { term tmp = h->keys[i]; h->keys[i] = key; key = tmp; }
          { uint32_t tmp = h->vals[i]; h->vals[i] = val; val = tmp; }
          set_isdel_true(oldflags, i);
        } else {
          h->keys[i] = key;
          h->vals[i] = val;
          break;
        }
      }
    }
  }

  free(oldflags);
  h->n_buckets = new_n_buckets;
  h->n_occupied = h->size;
  h->upper_bound = (uint32_t)(h->n_buckets * HASH_UPPER + 0.5);

  DEBUG("after bump, term hash at %p has size %u and boundary %p (+%ld)", h, termhash_size(h), h->boundary, (long)((uint8_t*)h->boundary - (uint8_t*)h));
  DEBUG("flags are at %p (+%ld)", h->flags, (long)((uint8_t*)h->flags - (uint8_t*)h->boundary));
  DEBUG(" keys are at %p (+%ld)", h->keys, (long)((uint8_t*)h->keys - (uint8_t*)h->boundary));
  DEBUG(" vals are at %p (+%ld)", h->vals, (long)((uint8_t*)h->vals - (uint8_t*)h->boundary));

#ifdef DEBUGOUTPUT
//DEBUG("and now i look like this:");
//termhash_dump(h);
#endif

  return NO_ERROR;
}

uint32_t termhash_put(termhash *h, term key, int *ret) {
  uint32_t x;

  {
#ifdef DEBUGOUTPUT
int num_loops = 0;
#endif
    uint32_t inc, k, i, site, last;
    x = site = h->n_buckets; k = hash_term(key); i = k % h->n_buckets;
    DEBUG("initial hash is %u", k);
    if (isempty(h->flags, i)) x = i;
    else {
      inc = 1 + k % (h->n_buckets - 1); last = i;
      while (!isempty(h->flags, i) && (isdel(h->flags, i) || !term_equals(h->keys[i], key))) {
#ifdef DEBUGOUTPUT
num_loops++;
#endif
        if (isdel(h->flags, i)) site = i;
        if (i + inc >= h->n_buckets) i = i + inc - h->n_buckets;
        else i += inc;
        if (i == last) { x = site; break; }
      }
      if ((x == h->n_buckets) && (i == last)) { // out of space
        if(!term_equals(h->keys[i], key)) {
          *ret = -1;
          return x;
        }
      }
      if (x == h->n_buckets) { // didn't find it on the first try
        if (isempty(h->flags, i) && site != h->n_buckets) x = site;
        else x = i;
      }
    }
    DEBUG("looped %u times to put", num_loops);
    //DEBUG("x is %u, site is %u, n_buckets is %u", x, site, h->n_buckets);
  }
  if (isempty(h->flags, x)) {
    h->keys[x] = key;
    set_isboth_false(h->flags, x);
    ++h->size; ++h->n_occupied;
    *ret = 1;
  } else if (isdel(h->flags, x)) {
    h->keys[x] = key;
    set_isboth_false(h->flags, x);
    ++h->size;
    *ret = 2;
  }
  else *ret = 0;

#ifdef DEBUGOUTPUT
//DEBUG("after put:");
//termhash_dump(h);
#endif

  return x;
}

void termhash_del(termhash *h, uint32_t x) {
  if (x != h->n_buckets && !iseither(h->flags, x)) {
    set_isdel_true(h->flags, x);
    --h->size;
  }
}

uint32_t termhash_get_val(termhash* h, term t) {
  uint32_t idx = termhash_get(h, t);
  if(idx == h->n_buckets) return (uint32_t)-1;
  return h->vals[idx];
}

wp_error* termhash_put_val(termhash* h, term t, uint32_t val) {
  int status;
  uint32_t loc = termhash_put(h, t, &status);
  DEBUG("put(%u,%u) has status %d and loc %u (error val is %u)", t.field_s, t.word_s, status, loc, h->n_buckets);
  if(status == -1) RAISE_ERROR("out of space in hash");
  h->vals[loc] = val;
  return NO_ERROR;
}

int termhash_needs_bump(termhash* h) {
  return (h->n_occupied >= h->upper_bound);
}

// returns the total size in bytes
//   memory layout: termhash, then:
//   ((n_buckets >> 4) + 1) uint32_t's for the flags
//   n_buckets terms for the keys
//   n_buckets uint32_t's for the vals (offsets into postings lists)
static uint32_t size(uint32_t n_buckets) {
  uint32_t size = sizeof(termhash) +
    (((n_buckets >> 4) + 1) * sizeof(uint32_t)) +
    (n_buckets * sizeof(term)) +
    (n_buckets * sizeof(uint32_t));

  DEBUG("size of a termhash with %u buckets is %lu + %lu + %lu + %lu = %u",
    n_buckets,
    (long)sizeof(termhash),
    (long)(((n_buckets >> 4) + 1) * sizeof(uint32_t)),
    (long)(n_buckets * sizeof(term)),
    (long)(n_buckets * sizeof(uint32_t)),
    size);

  return size;
}

uint32_t termhash_size(termhash* h) {
  return size(h->n_buckets);
}

uint32_t termhash_initial_size() {
  return size(prime_list[INITIAL_N_BUCKETS_IDX]);
}

uint32_t termhash_next_size(termhash* h) {
  int next_idx = (h->n_buckets_idx < (HASH_PRIME_SIZE - 1)) ? h->n_buckets_idx + 1 : h->n_buckets_idx;
  return size(prime_list[next_idx]);
}
