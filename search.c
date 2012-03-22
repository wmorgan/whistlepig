#include "whistlepig.h"

/********* search states *********/
typedef struct term_search_state {
  posting posting;
  int started;
  int done;
  int label; // 1 if a label; 0 if a term
} term_search_state;

typedef struct neg_search_state {
  docid_t next; // the next document in the child stream. we will never return this document.
  docid_t cur; // the last doc we returned
} neg_search_state;

#define DISJ_SEARCH_STATE_EMPTY 0
#define DISJ_SEARCH_STATE_FILLED 1
#define DISJ_SEARCH_STATE_DONE 2

typedef struct disj_search_state {
  docid_t last_docid;
  uint8_t* states; // whether the search result has been initialized or not
  search_result* results; // array of search results, one per child
} disj_search_state;

void wp_search_result_free(search_result* result) {
  for(int i = 0; i < result->num_doc_matches; i++) {
    //printf("for result at %p (dm %d), freeing positions at %p\n", result, i, result->doc_matches[i].positions);
    free(result->doc_matches[i].positions);
  }
  free(result->doc_matches);
}

RAISING_STATIC(search_result_init(search_result* result, const char* field, const char* word, posting* posting)) {
  result->doc_id = posting->doc_id;
  result->num_doc_matches = 1;
  result->doc_matches = malloc(sizeof(doc_match));
  result->doc_matches[0].field = field;
  result->doc_matches[0].word = word;
  result->doc_matches[0].num_positions = posting->num_positions;

  size_t size = sizeof(pos_t) * posting->num_positions;
  result->doc_matches[0].positions = malloc(size);
  //printf("for result at %p, allocated %u bytes for positions at %p\n", result, size, result->doc_matches[0].positions);
  memcpy(result->doc_matches[0].positions, posting->positions, size);

  return NO_ERROR;
}

RAISING_STATIC(search_result_combine_into(search_result* result, search_result* child_results, int num_child_results)) {
  if(num_child_results <= 0) RAISE_ERROR("no child results");
  result->doc_id = child_results[0].doc_id;
  result->num_doc_matches = num_child_results;
  result->doc_matches = malloc(sizeof(doc_match) * num_child_results);
  for(int i = 0; i < num_child_results; i++) {
    if(child_results[i].doc_matches == NULL) {
      result->doc_matches[i].field = NULL;
      result->doc_matches[i].word = NULL;
      result->doc_matches[i].num_positions = 0;
      result->doc_matches[i].positions = NULL;
    }
    else result->doc_matches[i] = child_results[i].doc_matches[0];
  }

  return NO_ERROR;
}

/*
 * we provide two functions for iterating through result streams: next() and
 * advance().
 *
 * next() returns results one at a time. it will set done = true if you're at
 * the end of the stream. otherwise, it will give you a result. the next
 * call to next() will give you the next result (or set done = true).
 *
 * advance() is given a docid and advances the stream to just *after* that
 * document, and tells you whether it saw the docid on the way (and sets the
 * result if so for your convenience).
 *
 * a next() followed by one or more advance() calls with the returned docid
 * will set found = true and will not advance the stream beyond where it
 * already is.
 *
 * however, an advance() to a docid followed by next() may skip a document in
 * the stream. you probably don't want this.
 *
 * so advance is only useful if you have a particular doc_id in mind, and you
 * want to see if this stream contains it. if you want to actually see all the
 * docids in a stream, you must use next().
 *
 */

/********** dispatch functions ***********/
static wp_error* term_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* conj_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* disj_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* phrase_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* neg_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* every_init_search_state(wp_query* q, wp_segment* s) RAISES_ERROR;
static wp_error* term_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* conj_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* disj_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* phrase_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* neg_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* every_release_search_state(wp_query* q) RAISES_ERROR;
static wp_error* term_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* conj_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* disj_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* phrase_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* neg_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* every_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) RAISES_ERROR;
static wp_error* term_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;
static wp_error* conj_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;
static wp_error* disj_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;
static wp_error* phrase_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;
static wp_error* neg_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;
static wp_error* every_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) RAISES_ERROR;

// the term_* functions also handle labels
// we use conj for empty queries as well (why not)
#define DISPATCH(type, suffix, ...) \
  switch(type) { \
    case WP_QUERY_TERM: \
    case WP_QUERY_LABEL: RELAY_ERROR(term_##suffix(__VA_ARGS__)); break; \
    case WP_QUERY_EMPTY: \
    case WP_QUERY_CONJ: RELAY_ERROR(conj_##suffix(__VA_ARGS__)); break; \
    case WP_QUERY_DISJ: RELAY_ERROR(disj_##suffix(__VA_ARGS__)); break; \
    case WP_QUERY_PHRASE: RELAY_ERROR(phrase_##suffix(__VA_ARGS__)); break; \
    case WP_QUERY_NEG: RELAY_ERROR(neg_##suffix(__VA_ARGS__)); break; \
    case WP_QUERY_EVERY: RELAY_ERROR(every_##suffix(__VA_ARGS__)); break; \
    default: RAISE_ERROR("unknown query node type %d", type); \
  } \

wp_error* wp_search_init_search_state(wp_query* q, wp_segment* s) {
  DISPATCH(q->type, init_search_state, q, s);
  return NO_ERROR;
}

wp_error* wp_search_release_search_state(wp_query* q) {
  DISPATCH(q->type, release_search_state, q)
  return NO_ERROR;
}

RAISING_STATIC(query_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done)) {
  DISPATCH(q->type, next_doc, q, s, result, done);
#ifdef DEBUGOUTPUT
    char buf[1024];
    wp_query_to_s(q, 1024, buf);

    if(*done) DEBUG("query %s is done", buf);
    else DEBUG("query %s has doc %u", buf, result->doc_id);
#endif
  return NO_ERROR;
}

RAISING_STATIC(query_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done)) {
  DISPATCH(q->type, advance_to_doc, q, s, doc_id, result, found, done);
#ifdef DEBUGOUTPUT
    char buf[1024];
    wp_query_to_s(q, 1024, buf);

    if(*done) DEBUG("query %s is done", buf);
    else {
      if(*found) DEBUG("query %s has doc %u", buf, doc_id);
      else DEBUG("query %s does not have doc %u", buf, doc_id);
    }
#endif
  return NO_ERROR;
}

/************** init functions *************/

RAISING_STATIC(init_children(wp_query* q, wp_segment* s)) {
  for(wp_query* child = q->children; child != NULL; child = child->next) RELAY_ERROR(wp_search_init_search_state(child, s));
  return NO_ERROR;
}

RAISING_STATIC(release_children(wp_query* q)) {
  for(wp_query* child = q->children; child != NULL; child = child->next) RELAY_ERROR(wp_search_release_search_state(child));
  return NO_ERROR;
}

static wp_error* term_init_search_state(wp_query* q, wp_segment* seg) {
  term t;
  stringmap* sh = MMAP_OBJ(seg->stringmap, stringmap);
  termhash* th = MMAP_OBJ(seg->termhash, termhash);

  term_search_state* state = q->search_data = malloc(sizeof(term_search_state));
  state->started = 0;

  state->label = q->type == WP_QUERY_LABEL ? 1 : 0;
  if(state->label) t.field_s = 0;
  else t.field_s = stringmap_string_to_int(sh, q->field); // will be -1 if not found

  t.word_s = stringmap_string_to_int(sh, q->word);

  uint32_t offset = termhash_get_val(th, t);
  if(offset == (uint32_t)-1) offset = OFFSET_NONE;

  if(offset == OFFSET_NONE) state->done = 1; // no entry in term hash
  else {
    state->done = 0;
    if(state->label) RELAY_ERROR(wp_segment_read_label(seg, offset, &state->posting));
    else RELAY_ERROR(wp_segment_read_posting(seg, offset, &state->posting, 1));
  }

  RELAY_ERROR(init_children(q, seg));

  return NO_ERROR;
}

static wp_error* term_release_search_state(wp_query* q) {
  term_search_state* state = q->search_data;
  if(!state->done) free(state->posting.positions);
  free(state);
  RELAY_ERROR(release_children(q));
  return NO_ERROR;
}

static wp_error* conj_init_search_state(wp_query* q, wp_segment* s) {
  q->search_data = NULL; // no state needed
  RELAY_ERROR(init_children(q, s));
  return NO_ERROR;
}

static wp_error* conj_release_search_state(wp_query* q) {
  RELAY_ERROR(release_children(q));
  return NO_ERROR;
}

static wp_error* disj_init_search_state(wp_query* q, wp_segment* s) {
  disj_search_state* state = q->search_data = malloc(sizeof(disj_search_state));
  state->states = NULL;
  state->results = NULL;
  state->last_docid = DOCID_NONE;
  RELAY_ERROR(init_children(q, s));
  return NO_ERROR;
}

static wp_error* disj_release_search_state(wp_query* q) {
  disj_search_state* state = (disj_search_state*)q->search_data;
  if(state->states) {
    // free any remaining search results in the buffer
    for(uint16_t i = 0; i < q->num_children; i++) {
      if(state->states[i] == DISJ_SEARCH_STATE_FILLED) wp_search_result_free(&state->results[i]);
    }
    free(state->states);
    free(state->results);
  }
  free(state);
  RELAY_ERROR(release_children(q));
  return NO_ERROR;
}

static wp_error* phrase_init_search_state(wp_query* q, wp_segment* s) {
  q->search_data = NULL; // no state needed
  RELAY_ERROR(init_children(q, s));
  return NO_ERROR;
}

static wp_error* phrase_release_search_state(wp_query* q) {
  RELAY_ERROR(release_children(q));
  return NO_ERROR;
}

static wp_error* neg_init_search_state(wp_query* q, wp_segment* seg) {
  if(q->num_children != 1) RAISE_ERROR("negations currently only operate on single children");

  RELAY_ERROR(wp_search_init_search_state(q->children, seg));

  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  neg_search_state* state = q->search_data = malloc(sizeof(neg_search_state));

  state->cur = si->num_docs + 1;
  search_result result;
  int done;
  RELAY_ERROR(query_next_doc(q->children, seg, &result, &done));
  if(done) state->next = DOCID_NONE;
  else {
    state->next = result.doc_id;
    wp_search_result_free(&result);
  }
  DEBUG("initialized with cur %u and next %u", state->cur, state->next);

  return NO_ERROR;
}

static wp_error* neg_release_search_state(wp_query* q) {
  RELAY_ERROR(wp_search_release_search_state(q->children));
  free(q->search_data);
  return NO_ERROR;
}

static wp_error* every_init_search_state(wp_query* q, wp_segment* seg) {
  q->search_data = malloc(sizeof(docid_t));

  segment_info* si = MMAP_OBJ(seg->seginfo, segment_info);
  *(docid_t*)q->search_data = si->num_docs;

  return NO_ERROR;
}

static wp_error* every_release_search_state(wp_query* q) {
  free(q->search_data);
  return NO_ERROR;
}

/********** search functions **********/

static wp_error* term_next_doc(wp_query* q, wp_segment* s, search_result* result, int* done) {
  term_search_state* state = (term_search_state*)q->search_data;

  DEBUG("[%s:'%s'] before: started is %d, done is %d", q->field, q->word, state->started, state->done);
  if(state->done) {
    *done = 1;
    return NO_ERROR;
  }

  *done = 0;
  if(!state->started) { // start
    state->started = 1;
    RELAY_ERROR(search_result_init(result, q->field, q->word, &state->posting));
  }
  else { // advance
    free(state->posting.positions);
    if(state->posting.next_offset == OFFSET_NONE) { // end of stream
      *done = state->done = 1;
    }
    else {
      if(state->label) RELAY_ERROR(wp_segment_read_label(s, state->posting.next_offset, &state->posting));
      else RELAY_ERROR(wp_segment_read_posting(s, state->posting.next_offset, &state->posting, 1));
      RELAY_ERROR(search_result_init(result, q->field, q->word, &state->posting));
    }
  }
  DEBUG("[%s:'%s'] after: doc id %u, done is %d, started is %d", q->field, q->word, (state->started && !state->done && result) ? result->doc_id : 0, *done, state->started);

  return NO_ERROR;
}

static wp_error* term_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) {
  term_search_state* state = (term_search_state*)q->search_data;
  DEBUG("[%s:'%s'] seeking through postings for doc %u", q->field, q->word, doc_id);

  if(state->done) { // end of stream
    *found = 0;
    *done = 1;
    return NO_ERROR;
  }

  while(state->posting.doc_id > doc_id) {
    free(state->posting.positions);
    DEBUG("skipping doc_id %u", state->posting.doc_id);
    if(state->posting.next_offset == OFFSET_NONE) {
      state->done = 1;
      break;
    }

    if(state->label) RELAY_ERROR(wp_segment_read_label(s, state->posting.next_offset, &state->posting));
    else RELAY_ERROR(wp_segment_read_posting(s, state->posting.next_offset, &state->posting, 1));
    //DEBUG("advanced posting to %p", state->posting);
  }

  if(state->done) {
    DEBUG("[%s:'%s'] posting list exhausted", q->field, q->word);
    *found = 0;
    *done = 1;
  }
  else {
    *done = 0;
    DEBUG("[%s:'%s'] posting advanced to that of doc %u", q->field, q->word, state->posting.doc_id);
    *found = (doc_id == state->posting.doc_id ? 1 : 0);
    if(*found) RELAY_ERROR(search_result_init(result, q->field, q->word, &state->posting));
  }

  return NO_ERROR;
}

// this advances all children *until* it finds a child that doesn't have the
// doc. at that point it stops. so it will return found=0 if any single child
// doesn't have the doc, and done=1 if any single child is done.
//
// this is used by both phrasal and conjunctive queries.
static wp_error* advance_all_children(wp_query* q, wp_segment* seg, docid_t search_doc, search_result* child_results, int* found, int* done) {
  int num_children_searched = 0;
  *found = 1;

  DEBUG("advancing all children to doc %u with early termination", search_doc);

  for(wp_query* child = q->children; child != NULL; child = child->next) {
    RELAY_ERROR(query_advance_to_doc(child, seg, search_doc, &child_results[num_children_searched], found, done));
    num_children_searched++;
    if(!*found) break;
  }

  if(!*found) for(int i = 0; i < num_children_searched - 1; i++) wp_search_result_free(&child_results[i]);

  return NO_ERROR;
}

static wp_error* disj_next_doc(wp_query* q, wp_segment* seg, search_result* result, int* done) {
  if(q->children == NULL) {
    *done = 1;
    return NO_ERROR;
  }

  // allocate search state if necessary
  disj_search_state* state = (disj_search_state*)q->search_data;
  if(state->states == NULL) {
    state->states = malloc(sizeof(uint8_t) * q->num_children);
    state->results = malloc(sizeof(search_result) * q->num_children);
    memset(state->states, DISJ_SEARCH_STATE_EMPTY, sizeof(uint8_t) * q->num_children);
  }

  // fill all the results we can into the buffer by calling next_doc on all
  // non-done children
  uint16_t i = 0;
  for(wp_query* child = q->children; child != NULL; child = child->next) {
    if(state->states[i] == DISJ_SEARCH_STATE_EMPTY) {
      int thisdone = 0;
      DEBUG("recursing on child %d", i);
      RELAY_ERROR(query_next_doc(child, seg, &(state->results[i]), &thisdone));
      if(thisdone == 1) state->states[i] = DISJ_SEARCH_STATE_DONE;
      else state->states[i] = DISJ_SEARCH_STATE_FILLED;
      DEBUG("after recurse, state %d is marked %d", i, state->states[i]);
    }
    i++;
  }

  // now find the largest
  uint16_t max_doc_idx = 0;
  docid_t max_docid = 0;

  *done = 1;
  i = 0;
  for(wp_query* child = q->children; child != NULL; child = child->next) {
    DEBUG("child %d is marked as %d", i, state->states[i]);
    if(state->states[i] == DISJ_SEARCH_STATE_FILLED) {
      if((*done == 1) || (state->results[i].doc_id > max_docid)) {
        if(state->results[i].doc_id == state->last_docid) { // discard dupes
          DEBUG("child %d has old result %u; voiding", i, state->last_docid);
          wp_search_result_free(&state->results[i]);
          state->states[i] = DISJ_SEARCH_STATE_EMPTY;
        }
        else {
          *done = 0;
          max_docid = state->results[i].doc_id;
          max_doc_idx = i;
        }
      }
    }
    i++;
  }

  // finally, copy the result
  if(*done == 0) {
    DEBUG("returning doc %d at index %d", max_docid, max_doc_idx);
    memcpy(result, &state->results[max_doc_idx], sizeof(search_result));
    state->states[max_doc_idx] = DISJ_SEARCH_STATE_EMPTY;
    state->last_docid = result->doc_id;
  }

  return NO_ERROR;
}

static wp_error* conj_next_doc(wp_query* q, wp_segment* seg, search_result* result, int* done) {
  docid_t search_doc;
  int found = 0;
  *done = 0;

  // start with the first child's first doc
  // TODO: find smallest postings list and use that instead
  wp_query* master = q->children;
  if(master == NULL) *done = 1;

  while(!found && !*done) {
    RELAY_ERROR(query_next_doc(master, seg, result, done));
    DEBUG("master reports doc %u done %d", result->doc_id, *done);
    if(!*done) {
      search_doc = result->doc_id;
      wp_search_result_free(result); // sigh
      RELAY_ERROR(conj_advance_to_doc(q, seg, search_doc, result, &found, done));
    }
    DEBUG("after search, found is %d and done is %d", found, *done);
  }

  return NO_ERROR;
}

static wp_error* conj_advance_to_doc(wp_query* q, wp_segment* s, docid_t doc_id, search_result* result, int* found, int* done) {
  search_result* child_results = malloc(sizeof(search_result) * q->num_children);
  RELAY_ERROR(advance_all_children(q, s, doc_id, child_results, found, done));

  if(*found) {
    DEBUG("successfully found doc %u", doc_id);
    RELAY_ERROR(search_result_combine_into(result, child_results, q->num_children));
  }

  free(child_results);
  return NO_ERROR;
}

static wp_error* disj_advance_to_doc(wp_query* q, wp_segment* seg, docid_t doc_id, search_result* result, int* found, int* done) {
  search_result child_result;
  int child_found;

  DEBUG("advancing all to %d", doc_id);

  *found = 0;
  *done = 0;
  uint16_t i = 0;
  for(wp_query* child = q->children; child != NULL; child = child->next) {
    int child_done;
    RELAY_ERROR(query_advance_to_doc(child, seg, doc_id, &child_result, &child_found, &child_done));
    DEBUG("child %u reports found %d and done %d", i, child_found, child_done);
    *done = *done && child_done; // we're only done if ALL children are done
    if(child_found && !*found) {
      *found = 1;
      *result = child_result;
    }

    i += 1;
    // TODO XXXXXXXXXX does this leak memory when multiple children all return results?
  }

#ifdef DEBUGOUTPUT
  if(*found) DEBUG("successfully found doc %u", doc_id);
  else DEBUG("did not find doc %u", doc_id);
#endif

  // now release any buffered results if they're > doc_id
  disj_search_state* state = (disj_search_state*)q->search_data;
  if(state->states != NULL) {
    uint16_t i = 0;
    for(wp_query* child = q->children; child != NULL; child = child->next) {
      if((state->states[i] == DISJ_SEARCH_STATE_FILLED) && (state->results[i].doc_id > doc_id)) {
        wp_search_result_free(&state->results[i]);
        state->states[i] = DISJ_SEARCH_STATE_EMPTY;
      }
      i++;
    }
  }

  return NO_ERROR;
}

// sadly, this is basically a copy of conj_next_doc right now. all the
// interesting phrasal checking is done by phrase_advance_to_doc.
static wp_error* phrase_next_doc(wp_query* q, wp_segment* seg, search_result* result, int* done) {
#ifdef DEBUGOUTPUT
  char query_s[1024];
  wp_query_to_s(q, 1024, query_s);
  DEBUG("called on %s", query_s);
#endif

  docid_t search_doc;
  int found = 0;
  *done = 0;

  // start with the first child's first doc
  // TODO: find smallest postings list and use that instead
  wp_query* master = q->children;
  if(master == NULL) *done = 1;

  while(!found && !*done) {
    RELAY_ERROR(query_next_doc(master, seg, result, done));
    DEBUG("master reports doc %u done %d", result->doc_id, *done);
    if(!*done) {
      search_doc = result->doc_id;
      wp_search_result_free(result); // sigh
      RELAY_ERROR(phrase_advance_to_doc(q, seg, search_doc, result, &found, done));
    }
    DEBUG("after search, found is %d and done is %d", found, *done);
  }

  return NO_ERROR;
}

static wp_error* phrase_advance_to_doc(wp_query* q, wp_segment* seg, docid_t doc_id, search_result* result, int* found, int* done) {
#ifdef DEBUGOUTPUT
  char query_s[1024];
  wp_query_to_s(q, 1024, query_s);
  DEBUG("called on %s", query_s);
#endif

  search_result* child_results = malloc(sizeof(search_result) * q->num_children);

  DEBUG("will be searching for doc %u", doc_id);
  RELAY_ERROR(advance_all_children(q, seg, doc_id, child_results, found, done));

  if(*found) {
    DEBUG("found doc %u. now checking for positional matches", doc_id);

    // TODO remove this once we're less paranoid
    for(int i = 0; i < q->num_children; i++) {
      if(child_results[i].num_doc_matches != 1) RAISE_ERROR("invalid state: %d results", child_results[i].num_doc_matches);
      if(child_results[i].doc_id != doc_id) RAISE_ERROR("invalid state: doc id %u vs searched-for %u", child_results[i].doc_id, doc_id);
    }

    /* the following can be optimized in several ways:

       1. choose the doc with the smallest number of term matches, rather than aways picking the first.
       2. do a binary search to find the position (since the array is sorted), rather than a linear
          scan.

      this is simply the simplest, stupidest, first-approach implementation.
    */

    // we'll base everything off of this guy
    doc_match* first_dm = &child_results[0].doc_matches[0];

    // allocate enough space to hold the maximum number of positions
    pos_t* phrase_positions = malloc(sizeof(pos_t) * first_dm->num_positions);
    int num_positions_found = 0;

    for(int i = 0; i < first_dm->num_positions; i++) {
      pos_t position = first_dm->positions[i];
      DEBUG("try %d: match by term 0 at position %u", i, position);

      int found_in_this_position = 1;
      for(int j = 1; j < q->num_children; j++) {
        doc_match* this_dm = &child_results[j].doc_matches[0];

        int k, found_in_doc = 0;
        for(k = 0; k < this_dm->num_positions; k++) {
          if(this_dm->positions[k] == (position + j)) {
            found_in_doc = 1;
            break;
          }
        }

        if(!found_in_doc) {
          found_in_this_position = 0;
          DEBUG("term %d did NOT match at position %u after %d comparisons", j, position + j, k + 1);
          break;
        }
#ifdef DEBUGOUTPUT
        else DEBUG("term %d matched at position %u after %d/%d comparisons", j, position + j, k + 1, this_dm->num_positions);
#endif
      }

      if(found_in_this_position) phrase_positions[num_positions_found++] = position; // got a match!
    }

    if(num_positions_found > 0) {
      // fill in the result
      result->doc_id = doc_id;
      result->num_doc_matches = 1;
      result->doc_matches = malloc(sizeof(doc_match));
      result->doc_matches[0].field = NULL;
      result->doc_matches[0].word = NULL;
      result->doc_matches[0].num_positions = num_positions_found;
      result->doc_matches[0].positions = phrase_positions;
    }
    else {
      *found = 0;
      free(phrase_positions);
    }
    for(int i = 0; i < q->num_children; i++) wp_search_result_free(&child_results[i]);
  }

  free(child_results);
  return NO_ERROR;
}

static wp_error* neg_next_doc(wp_query* q, wp_segment* seg, search_result* result, int* done) {
  neg_search_state* state = (neg_search_state*)q->search_data;

  DEBUG("called with cur %u and next %u", state->cur, state->next);

  if(state->cur == DOCID_NONE) {
    *done = 1;
    return NO_ERROR;
  }

  state->cur--; // advance virtual doc pointer

  // if state->cur == state->next, we need to load the substream's next
  // document, decrement our cur, and recheck.
  while((state->cur > DOCID_NONE) && (state->cur == state->next)) { // need to advance the child stream
    state->cur--; // can't use the previous value because == next; decrement

    int child_done;
    RELAY_ERROR(query_next_doc(q->children, seg, result, &child_done));
    if(child_done) state->next = DOCID_NONE; // child stream is done
    else {
      state->next = result->doc_id;
      wp_search_result_free(result);
    }

    DEBUG("after bump, cur %u and next %u", state->cur, state->next);
  }

  // check again... sigh
  if(state->cur == DOCID_NONE) {
    *done = 1;
    return NO_ERROR;
  }

  DEBUG("returning doc %u", state->cur);
  result->doc_id = state->cur;
  result->num_doc_matches = 0;
  result->doc_matches = NULL;
  *done = 0;
  return NO_ERROR;
}

static wp_error* neg_advance_to_doc(wp_query* q, wp_segment* seg, docid_t doc_id, search_result* result, int* found, int* done) {
  neg_search_state* state = (neg_search_state*)q->search_data;

  DEBUG("in search for %u, called with cur %u and next %u", doc_id, state->cur, state->next);

  if(state->cur == DOCID_NONE) {
    *done = 1;
    *found = 0;
    return NO_ERROR;
  }

  // seek through child stream until we find a docid it contains that's <= doc_id
  while(state->next > doc_id) { // need to advance child stream
    int child_done;
    RELAY_ERROR(query_next_doc(q->children, seg, result, &child_done));
    if(child_done) state->next = DOCID_NONE; // will break the loop too
    else state->next = result->doc_id;
  }

  DEBUG("in search for %u, intermediate state is cur %u and next %u", doc_id, state->cur, state->next);

  // at this point we know state->next, our child pointer, is <= doc_id
  state->cur = doc_id;
  if(state->next == doc_id) *found = 0; // opposite day
  else {
    *found = 1;
    result->doc_id = doc_id;
    result->num_doc_matches = 0;
    result->doc_matches = NULL;
  }

  *done = state->cur == DOCID_NONE ? 1 : 0;

  DEBUG("finally, state is cur %u and next %u and found is %d and done is %d", state->cur, state->next, *found, *done);
  return NO_ERROR;
}

static wp_error* every_next_doc(wp_query* q, wp_segment* seg, search_result* result, int* done) {
  (void)seg; // don't actually need to look in here!
  docid_t* state_doc_id = (docid_t*)q->search_data;

  DEBUG("called with cur %u", *state_doc_id);

  if(*state_doc_id == DOCID_NONE) {
    *done = 1;
  }
  else {
    result->doc_id = *state_doc_id;
    result->num_doc_matches = 0;
    result->doc_matches = NULL;
    (*state_doc_id)--;
    *done = 0;
  }
  return NO_ERROR;
}

static wp_error* every_advance_to_doc(wp_query* q, wp_segment* seg, docid_t doc_id, search_result* result, int* found, int* done) {
  (void)seg; // don't actually need to look in here!
  docid_t* state_doc_id = q->search_data;

  DEBUG("called with cur %u", *state_doc_id);

  if(*state_doc_id == DOCID_NONE) {
    *found = 0;
  }
  else {
    *state_doc_id = doc_id - 1; // just after that doc
    *found = 1; // we find everyhing
    result->doc_id = doc_id;
    result->num_doc_matches = 0;
    result->doc_matches = NULL;
  }

  *done = (*state_doc_id == DOCID_NONE ? 1 : 0);
  return NO_ERROR;
}

wp_error* wp_search_run_query_on_segment(struct wp_query* q, struct wp_segment* s, uint32_t max_num_results, uint32_t* num_results, search_result* results) {
  int done;

  *num_results = 0;

#ifdef DEBUG
  char buf[1024];
  wp_query_to_s(q, 1024, buf);
  DEBUG("running query %s", buf);
#endif

  while(*num_results < max_num_results) {
    DEBUG("got %d results so far (max is %d)", *num_results, max_num_results);
    RELAY_ERROR(query_next_doc(q, s, &results[*num_results], &done));
    if(done) break;
    DEBUG("got result %u (%u doc matches)", results[*num_results].doc_id, results[*num_results].num_doc_matches);
    (*num_results)++;
    DEBUG("num results now %d", *num_results);
  }

  return NO_ERROR;
}

