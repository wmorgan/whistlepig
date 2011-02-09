#include "whistlepig.h"
#include "tokenizer.lex.h"

static posarray* posarray_new(pos_t i) {
  posarray* ret = malloc(sizeof(posarray));
  ret->data = malloc(sizeof(pos_t));
  ret->data[0] = i;
  ret->size = ret->next = 1;
  return ret;
}

static void posarray_free(posarray* p) {
  free(p->data);
  free(p);
}

static void posarray_add(posarray* p, pos_t a) {
  while(p->next >= p->size) {
    p->size *= 2;
    p->data = realloc(p->data, p->size * sizeof(pos_t));
  }
  p->data[p->next++] = a;
}

static inline pos_t posarray_get(posarray* p, int i) { return p->data[i]; }

static inline khint_t khash_hash_string(const char *s) {
  khint_t h = *s;
  if (h) for (++s ; *s; ++s) h = (h << 5) - h + *s;
  return h;
}

inline khint_t fielded_term_hash(fielded_term ft) {
  return khash_hash_string(ft.field) ^ khash_hash_string(ft.term);
}

inline khint_t fielded_term_equals(fielded_term a, fielded_term b) {
  return (strcmp(a.field, b.field) == 0) && (strcmp(a.term, b.term) == 0);
}

wp_entry* wp_entry_new() {
  wp_entry* ret = malloc(sizeof(wp_entry));
  ret->entries = kh_init(entries);
  ret->next_offset = 0;

  return ret;
}

RAISING_STATIC(add_token(wp_entry* entry, const char* field, const char* term, int field_len, int term_len)) {
  fielded_term ft;
  int status;

  // copy field and term
  ft.field = calloc(field_len + 1, sizeof(char));
  strncpy(ft.field, field, field_len);

  ft.term = calloc(term_len + 1, sizeof(char));
  strncpy(ft.term, term, term_len);

  khiter_t k = kh_put(entries, entry->entries, ft, &status);
  if(status == 1) { // not found
    kh_value(entry->entries, k) = posarray_new(entry->next_offset);
  }
  else { // just add the next offset to the array
    posarray_add(kh_value(entry->entries, k), entry->next_offset);

    // don't need these guys any more
    free(ft.field);
    free(ft.term);
  }

  entry->next_offset++;

  return NO_ERROR;
}

uint32_t wp_entry_size(wp_entry* entry) {
  uint32_t ret = 0;

  for(khiter_t i = kh_begin(entry->entries); i < kh_end(entry->entries); i++) {
    if(kh_exist(entry->entries, i)) {
      posarray* positions = kh_val(entry->entries, i);
      ret += positions->next;
    }
  }

  return ret;
}

RAISING_STATIC(add_from_lexer(wp_entry* entry, yyscan_t* scanner, const char* field)) {
  int token_type;
  int field_len = strlen(field);

  do {
    token_type = yylex(*scanner);
    if(token_type == TOK_WORD) {
      RELAY_ERROR(add_token(entry, field, yyget_text(*scanner), field_len, yyget_leng(*scanner)));
    }
  } while(token_type != TOK_DONE);

  return NO_ERROR;
}

wp_error* wp_entry_add_token(wp_entry* entry, const char* field, const char* term) {
  RELAY_ERROR(add_token(entry, field, term, strlen(field), strlen(term)));

  return NO_ERROR;
}

// tokenizes and adds everything under a single field
wp_error* wp_entry_add_string(wp_entry* entry, const char* field, const char* string) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);
  YY_BUFFER_STATE state = yy_scan_string(string, scanner);
  RELAY_ERROR(add_from_lexer(entry, &scanner, field));
  yy_delete_buffer(state, scanner);
  yylex_destroy(scanner);

  return NO_ERROR;
}

// tokenizes and adds everything from a file under a single field
wp_error* wp_entry_add_file(wp_entry* entry, const char* field, FILE* f) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);
  yyset_in(f, scanner);
  RELAY_ERROR(add_from_lexer(entry, &scanner, field));
  yylex_destroy(scanner);

  return NO_ERROR;
}

wp_error* wp_entry_write_to_segment(wp_entry* entry, wp_segment* seg, docid_t doc_id) {
  for(khiter_t i = kh_begin(entry->entries); i < kh_end(entry->entries); i++) {
    if(kh_exist(entry->entries, i)) {
      fielded_term ft = kh_key(entry->entries, i);
      posarray* positions = kh_val(entry->entries, i);
      RELAY_ERROR(wp_segment_add_posting(seg, ft.field, ft.term, doc_id, positions->next, positions->data));
    }
  }

  return NO_ERROR;
}

// currently this is a crazy overestimate (it's calculating the size without
// VBE) but that's fine. as long as we're not an underestimate, we should be ok.
wp_error* wp_entry_sizeof_postings_region(wp_entry* entry, wp_segment* seg, uint32_t* size) {
  *size = 0;
  for(khiter_t i = kh_begin(entry->entries); i < kh_end(entry->entries); i++) {
    if(kh_exist(entry->entries, i)) {
      posarray* positions = kh_val(entry->entries, i);

      uint32_t this_size;
      RELAY_ERROR(wp_segment_sizeof_posarray(seg, positions->next, positions->data, &this_size));
      *size += this_size;
    }
  }

  return NO_ERROR;
}

wp_error* wp_entry_free(wp_entry* entry) {
  for(khiter_t k = kh_begin(entry->entries); k < kh_end(entry->entries); k++) {
    if(kh_exist(entry->entries, k)) {
      fielded_term ft = kh_key(entry->entries, k);
      posarray* positions = kh_val(entry->entries, k);
      free(ft.term);
      free(ft.field);
      posarray_free(positions);
    }
  }

  kh_destroy(entries, entry->entries);
  free(entry);

  return NO_ERROR;
}
