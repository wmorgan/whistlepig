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

static inline uint32_t string_hash(const char *s) {
  uint32_t h = *s;
  if (h) for (++s ; *s; ++s) h = (h << 5) - h + *s;
  return h;
}

static inline int string_equals(const char* a, const char* b) {
  //DEBUG("comparing '%s' (%p) and '%s' (%p)", a, a, b, b);
  return strcmp(a, b) == 0;
}

// set flags, keys and vals to correct locations based on h->n_buckets
void stringmap_setup(stringmap* h, stringpool* p) {
  h->pool = p;
  h->flags = (uint32_t*)h->boundary;
  h->keys = (uint32_t*)((uint32_t*)h->boundary + ((h->n_buckets >> 4) + 1));
}

void stringmap_init(stringmap* h, stringpool* p) {
  h->n_buckets_idx = INITIAL_N_BUCKETS_IDX;
  h->n_buckets = prime_list[h->n_buckets_idx];
  h->upper_bound = (uint32_t)(h->n_buckets * HASH_UPPER + 0.5);
  h->size = h->n_occupied = 0;
  stringmap_setup(h, p);
  memset(h->flags, 0xaa, ((h->n_buckets>>4) + 1) * sizeof(uint32_t));
}

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

uint32_t stringmap_get(stringmap *h, const char* key) {
  if(h->n_buckets) {
    uint32_t inc, k, i, last;
    k = string_hash(key); i = k % h->n_buckets;
    inc = 1 + k % (h->n_buckets - 1); last = i;
    while (!isempty(h->flags, i) && (isdel(h->flags, i) || !string_equals(stringpool_lookup(h->pool, h->keys[i]), key))) {
      if (i + inc >= h->n_buckets) i = i + inc - h->n_buckets;
      else i += inc;
      if (i == last) return h->n_buckets;
    }
    return iseither(h->flags, i)? h->n_buckets : i;
  }
  else return 0;
}

wp_error* stringmap_bump_size(stringmap *h) {
  DEBUG("bumping size for string hash at %p with size %u and boundary %p", h, stringmap_size(h), h->boundary);

  if(h->n_buckets_idx >= (HASH_PRIME_SIZE - 1)) RAISE_ERROR("stringmap can't be this big");

  h->n_buckets_idx++;
  uint32_t new_n_buckets = prime_list[h->n_buckets_idx];

  // first make a backup of the oldflags
  size_t oldflagsize = ((h->n_buckets >> 4) + 1) * sizeof(uint32_t);
  uint32_t* oldflags = malloc(oldflagsize);
  memcpy(oldflags, h->flags, oldflagsize);

  // keep pointers to the old locations
  uint32_t* oldkeys = h->keys;

  // set pointers to the new locations
  h->keys = (uint32_t*)((uint32_t*)h->boundary + ((new_n_buckets >> 4) + 1));

  // move the keys
  memmove(h->keys, oldkeys, h->n_buckets * sizeof(uint32_t));

  // clear the new flags
  memset(h->flags, 0xaa, ((new_n_buckets>>4) + 1) * sizeof(uint32_t));

  // do the complicated stuff from khash.h
  for (unsigned int j = 0; j != h->n_buckets; ++j) {
    if (iseither(oldflags, j) == 0) {
      uint32_t key = h->keys[j];
      set_isdel_true(oldflags, j);
      while (1) {
        uint32_t inc, k, i;
        k = string_hash(stringpool_lookup(h->pool, key));
        i = k % new_n_buckets;
        inc = 1 + k % (new_n_buckets - 1);
        while (!isempty(h->flags, i)) {
          if (i + inc >= new_n_buckets) i = i + inc - new_n_buckets;
          else i += inc;
        }
        set_isempty_false(h->flags, i);
        if (i < h->n_buckets && iseither(oldflags, i) == 0) {
          { uint32_t tmp = h->keys[i]; h->keys[i] = key; key = tmp; }
          set_isdel_true(oldflags, i);
        } else {
          h->keys[i] = key;
          break;
        }
      }
    }
  }

  free(oldflags);
  h->n_buckets = new_n_buckets;
  h->n_occupied = h->size;
  h->upper_bound = (uint32_t)(h->n_buckets * HASH_UPPER + 0.5);

#ifdef DEBUGOUTPUT
  DEBUG("after bump, string hash at %p has size %u and boundary %p", h, stringmap_size(h), h->boundary);
#endif

  return NO_ERROR;
}

uint32_t stringmap_put(stringmap *h, const char* key, int *ret) {
  uint32_t x;

  {
#ifdef DEBUGOUTPUT
int num_loops = 0;
#endif
    uint32_t inc, k, i, site, last;
    x = site = h->n_buckets; k = string_hash(key); i = k % h->n_buckets;
    //DEBUG("asked to hash '%s'. initial hash is %u => %u and n_occupied is %u", key, k, i, h->n_occupied);
    if (isempty(h->flags, i)) x = i;
    else {
      inc = 1 + k % (h->n_buckets - 1); last = i;
      while (!isempty(h->flags, i) && (isdel(h->flags, i) || !string_equals(stringpool_lookup(h->pool, h->keys[i]), key))) {
#ifdef DEBUGOUTPUT
num_loops++;
#endif
        if (isdel(h->flags, i)) site = i;
        if (i + inc >= h->n_buckets) i = i + inc - h->n_buckets;
        else i += inc;
        if (i == last) { x = site; break; }
      }
      if ((x == h->n_buckets) && (i == last)) { // out of space
        if(!string_equals(stringpool_lookup(h->pool, h->keys[i]), key)) {
          DEBUG("out of space!");
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

  //DEBUG("for pos %u, isempty? %d and isdel %d", x, isempty(h->flags, x), isdel(h->flags, x));

  uint32_t idx;
  if(isempty(h->flags, x) || isdel(h->flags, x)) {
    idx = stringpool_add(h->pool, key);
    if(idx == (uint32_t)-1) {
      *ret = -2;
      return x;
    }
    if (isempty(h->flags, x)) ++h->n_occupied;
    h->keys[x] = idx;
    set_isboth_false(h->flags, x);
    ++h->size;
    *ret = 1;
  }
  else *ret = 0;

  return x;
}

void stringmap_del(stringmap *h, uint32_t x) {
  if (x != h->n_buckets && !iseither(h->flags, x)) {
    set_isdel_true(h->flags, x);
    --h->size;
  }
}

/*
uint32_t stringmap_get_val(stringmap* h, string t) {
  uint32_t idx = termhash_get(h, t);
  if(idx == h->n_buckets) return (uint32_t)-1;
  return h->vals[idx];
}

wp_error* termhash_put_val(termhash* h, term t, uint32_t val) {
  int status;
  uint32_t loc = termhash_put(h, t, &status);
  DEBUG("put(%u,%u) has status %d and ret %u (error val is %u)", t.field_s, t.word_s, status, loc, h->n_buckets);
  if(status == -1) RAISE_ERROR("out of space in hash");
  h->vals[loc] = val;
  return NO_ERROR;
}
*/

int stringmap_needs_bump(stringmap* h) {
  return (h->n_occupied >= h->upper_bound);
}

//   memory layout: stringmap, then:
//   ((n_buckets >> 4) + 1) uint32_t's for the flags
//   n_buckets uint32_t for the keys
static uint32_t size(uint32_t n_buckets) {
  uint32_t size = sizeof(stringmap) +
    (((n_buckets >> 4) + 1) * sizeof(uint32_t)) +
    (n_buckets * sizeof(uint32_t));
  return size;
}

// returns the total size in bytes
uint32_t stringmap_size(stringmap* h) {
  return size(h->n_buckets);
}

uint32_t stringmap_initial_size() {
  return size(prime_list[INITIAL_N_BUCKETS_IDX]);
}

// the size if we embiggen by one notch
uint32_t stringmap_next_size(stringmap* h) {
  int next_idx = (h->n_buckets_idx < (HASH_PRIME_SIZE - 1)) ? h->n_buckets_idx + 1 : h->n_buckets_idx;
  return size(prime_list[next_idx]);
}

const char* stringmap_int_to_string(stringmap* h, uint32_t i) {
  return stringpool_lookup(h->pool, i);
}

// returns -1 if not found
uint32_t stringmap_string_to_int(stringmap* h, const char* s) {
  uint32_t idx = stringmap_get(h, s);
  if(idx == h->n_buckets) return (uint32_t)-1; // not there
  return h->keys[idx];
}

wp_error* stringmap_add(stringmap *h, const char* s, uint32_t* id) {
  int status;
  uint32_t idx = stringmap_put(h, s, &status);
  if(status == -1) RAISE_ERROR("out of space in hash put");
  if(status == -2) RAISE_ERROR("out of space in pool put");

  *id = h->keys[idx];

  return NO_ERROR;
}
