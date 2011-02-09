#include <stdio.h>
#include <ruby.h>
#include "whistlepig.h"

static VALUE m_whistlepig;
static VALUE c_index;
static VALUE c_entry;
static VALUE c_query;
static VALUE c_error;
static VALUE c_parseerror;

static char* strdup(const char* old) { // wtf stupid
  size_t len = strlen(old) + 1;
  char *new = malloc(len * sizeof(char));
  return (char *)memcpy(new, old, len);
}

static void index_free(wp_index* index) {
  wp_error* e = wp_index_free(index);
  //printf("# index free at %p with error %p\n", index, e);
  if(e != NULL) {
    PRINT_ERROR(e, stderr); // why not?
    wp_error_free(e);
  }
}

#define RAISE_IF_NECESSARY(e) do { \
  if(e != NULL) { \
    VALUE exc = rb_exc_new2(c_error, e->msg); \
    wp_error_free(e); \
    rb_exc_raise(exc); \
  }  \
} while(0)

// support 1.9 and 1.8
#ifndef RSTRING_PTR
#define RSTRING_PTR(v) RSTRING(v)->ptr
#endif

/*
 * call-seq: Index.new(pathname_base)
 *
 * Creates or loads a new index. The on-disk representation will be multiple
 * files starting * with +pathname_base+.
 *
 * The index may be later be explicitly closed with Index#close. It will also
 * be automatically closed when Ruby exits.
 *
 */

static VALUE index_new(VALUE class, VALUE v_pathname_base) {
  Check_Type(v_pathname_base, T_STRING);

  wp_index* index;
  wp_error* e;
  char* pathname_base = RSTRING_PTR(v_pathname_base);

  if(wp_index_exists(pathname_base)) e = wp_index_load(&index, strdup(pathname_base));
  else e = wp_index_create(&index, strdup(pathname_base));
  RAISE_IF_NECESSARY(e);

  VALUE o_index = Data_Wrap_Struct(class, NULL, index_free, index);
  VALUE argv[1] = { v_pathname_base };
  rb_obj_call_init(o_index, 1, argv);
  return o_index;
}

/*
 * call-seq: Index.create(pathname_base)
 *
 * Creates a new index, raising an error if it already exists. The on-disk
 * representation will be multiple files starting with
 * +pathname_base+.
 *
 */

static VALUE index_create(VALUE class, VALUE v_pathname_base) {
  Check_Type(v_pathname_base, T_STRING);

  wp_index* index;
  wp_error* e = wp_index_create(&index, strdup(RSTRING_PTR(v_pathname_base)));
  //printf("# index create at %p, error is %p\n", index, e);
  RAISE_IF_NECESSARY(e);

  VALUE o_index = Data_Wrap_Struct(class, NULL, index_free, index);
  VALUE argv[1] = { v_pathname_base };
  rb_obj_call_init(o_index, 1, argv);
  return o_index;
}

/*
 * call-seq: Index.load(pathname_base)
 *
 * Loads a new index, raising an error if it doesn't exists. The on-disk *
 * representation will be multiple files starting with
 * +pathname_base+.
 *
 */

static VALUE index_load(VALUE class, VALUE v_pathname_base) {
  Check_Type(v_pathname_base, T_STRING);

  wp_index* index;
  wp_error* e = wp_index_load(&index, strdup(RSTRING_PTR(v_pathname_base)));
  //printf("# index load at %p, error is %p\n", index, e);
  RAISE_IF_NECESSARY(e);

  VALUE o_index = Data_Wrap_Struct(class, NULL, index_free, index);
  VALUE argv[1] = { v_pathname_base };
  rb_obj_call_init(o_index, 1, argv);
  return o_index;
}

/*
 * call-seq: Index.exists?(pathname_base)
 *
 * Returns true iff an index with base pathname of +pathname_base+
 * exists on disk.
 *
 */
static VALUE index_exists(VALUE class, VALUE v_pathname_base) {
  Check_Type(v_pathname_base, T_STRING);

  if(wp_index_exists(RSTRING_PTR(v_pathname_base))) return Qtrue;
  else return Qfalse;
}

/*
 * call-seq: Index.delete!(pathname_base)
 *
 * Deletes the index with base pathname +pathname_base+ from disk.
 * Does nothing if the index does not exist. If that index is currently loaded
 * in memory, expect may to see segfaults when you try to access it.
 *
 */
static VALUE index_delete(VALUE class, VALUE v_pathname_base) {
  Check_Type(v_pathname_base, T_STRING);

  wp_error* e = wp_index_delete(RSTRING_PTR(v_pathname_base));
  RAISE_IF_NECESSARY(e);

  return v_pathname_base;
}

/*
 * Returns the number of entries in the index.
 *
 */
static VALUE index_size(VALUE self) {
  wp_index* index;
  Data_Get_Struct(self, wp_index, index);
  return INT2NUM(wp_index_num_docs(index));
}

static VALUE index_init(VALUE self, VALUE v_pathname_base) {
  rb_iv_set(self, "@pathname_base", v_pathname_base);
  return self;
}

/*
 * call-seq: count(query)
 *
 * Returns the number of entries matched by +query+, which should be a Query object.
 * Note that in the current implementation, this is almost as expensive as retrieving all the
 * results directly.
 *
 */
static VALUE index_count(VALUE self, VALUE v_query) {
  if(CLASS_OF(v_query) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_query* query; Data_Get_Struct(v_query, wp_query, query);
  uint32_t num_results;
  // clone the query because we don't want to interrupt any search state
  // which may otherwise be being used for pagination.
  wp_error* e = wp_index_count_results(index, wp_query_clone(query), &num_results);
  RAISE_IF_NECESSARY(e);

  return INT2NUM(num_results);
}

/*
 * Closes the index, flushing all changes to disk. Future calls to this index
 * may result in a segfault.
 *
 */
static VALUE index_close(VALUE self) {
  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_error* e = wp_index_unload(index);
  RAISE_IF_NECESSARY(e);

  return Qnil;
}

static void entry_free(wp_entry* entry) {
  wp_error* e = wp_entry_free(entry);
  //printf("# entry free at %p with error %p\n", entry, e);
  if(e != NULL) {
    PRINT_ERROR(e, stderr); // why not?
    wp_error_free(e);
  }
}

/* Creates a new, empty entry. */
static VALUE entry_new(VALUE class) {
  wp_entry* entry = wp_entry_new();

  //printf("# entry create at %p\n", entry);
  VALUE o_entry = Data_Wrap_Struct(class, NULL, entry_free, entry);
  rb_obj_call_init(o_entry, 0, NULL);
  return o_entry;
}

/*
 * call-seq: add_token(field, token)
 *
 * Adds a single token +token+ with field +field</field> to an entry. Both
 * +token+ and +field</field> must be strings.
 *
 * Returns itself.
 */
static VALUE entry_add_token(VALUE self, VALUE field, VALUE term) {
  Check_Type(field, T_STRING);
  Check_Type(term, T_STRING);

  wp_entry* entry; Data_Get_Struct(self, wp_entry, entry);
  wp_error* e = wp_entry_add_token(entry, RSTRING_PTR(field), RSTRING_PTR(term));
  RAISE_IF_NECESSARY(e);

  return self;
}

/*
 * call-seq: add_string(field, string)
 *
 * Adds a String +string+ with field +field</field> to an entry. The string
 * will be tokenized on whitespace. Both +token+ and +string</field> must be
 * strings.
 *
 * Returns itself.
 */
static VALUE entry_add_string(VALUE self, VALUE field, VALUE string) {
  Check_Type(field, T_STRING);
  Check_Type(string, T_STRING);

  wp_entry* entry; Data_Get_Struct(self, wp_entry, entry);
  wp_error* e = wp_entry_add_string(entry, RSTRING_PTR(field), RSTRING_PTR(string));
  RAISE_IF_NECESSARY(e);

  return self;
}

/*
 * Returns the number of tokens in the entry.
 */
static VALUE entry_size(VALUE self) {
  wp_entry* entry; Data_Get_Struct(self, wp_entry, entry);
  return INT2NUM(wp_entry_size(entry));
}

/*
 * call-seq: add_entry(entry)
 *
 * Adds the entry +entry+ to the index. Returns the document id
 * corresponding to this entry.
 */
static VALUE index_add_entry(VALUE self, VALUE v_entry) {
  if(CLASS_OF(v_entry) != c_entry) {
    rb_raise(rb_eTypeError, "entry must be a Whistlepig::Entry object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_entry* entry; Data_Get_Struct(v_entry, wp_entry, entry);
  uint64_t doc_id;
  wp_error* e = wp_index_add_entry(index, entry, &doc_id);
  RAISE_IF_NECESSARY(e);

  return INT2NUM(doc_id);
}

/*
 * call-seq: add_label(doc_id, label)
 *
 * Adds the label +label+ to the document corresponding to doc id
 * +doc_id+ in the index. +label+ must be a String.
 * If the label has already been added to the document, does nothing.
 */
static VALUE index_add_label(VALUE self, VALUE v_doc_id, VALUE v_label) {
  Check_Type(v_doc_id, T_FIXNUM);
  Check_Type(v_label, T_STRING);

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_error* e = wp_index_add_label(index, RSTRING_PTR(v_label), NUM2INT(v_doc_id));
  RAISE_IF_NECESSARY(e);

  return v_label;
}

/*
 * call-seq: remove_label(doc_id, label)
 *
 * Removes the label +label+ from the document corresponding to doc id
 * +doc_id+ in the index. +label+ must be a String.
 * If the label has not been added to the document, does nothing.
 */
static VALUE index_remove_label(VALUE self, VALUE v_doc_id, VALUE v_label) {
  Check_Type(v_doc_id, T_FIXNUM);
  Check_Type(v_label, T_STRING);

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_error* e = wp_index_remove_label(index, RSTRING_PTR(v_label), NUM2INT(v_doc_id));
  RAISE_IF_NECESSARY(e);

  return v_label;
}

/*
 * call-seq: Query.new(default_field, query_string)
 *
 * Creates a new query by parsing the string +query_string+, which must be a
 * String.  Any non-fielded terms will used the field +default_field+, which
 * must also be a String. Raises a ParseError if the query cannot be parsed.
 *
 */
static VALUE query_new(VALUE class, VALUE default_field, VALUE string) {
  Check_Type(default_field, T_STRING);
  Check_Type(string, T_STRING);

  wp_query* query;
  wp_error* e = wp_query_parse(RSTRING_PTR(string), RSTRING_PTR(default_field), &query);
  if(e != NULL) {
    VALUE exc = rb_exc_new2(c_parseerror, e->msg);
    wp_error_free(e);
    rb_exc_raise(exc);
  }

  VALUE o_query = Data_Wrap_Struct(class, NULL, wp_query_free, query);
  VALUE argv[2] = { string, default_field };
  rb_obj_call_init(o_query, 2, argv);

  return o_query;
}

/*
 * Returns a parsed representation of a String, useful for debugging.
 */
static VALUE query_to_s(VALUE self) {
  char buf[1024];

  wp_query* query; Data_Get_Struct(self, wp_query, query);
  wp_query_to_s(query, 1024, buf);

  return rb_str_new2(buf);
}

/*
 * call-seq: and(other)
 *
 * Returns a new Query that is a conjunction of this query and +other+, which
 * must also be a Query object.
 *
 */
static VALUE query_and(VALUE self, VALUE v_other) {
  if(CLASS_OF(v_other) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_query* query; Data_Get_Struct(self, wp_query, query);
  wp_query* other; Data_Get_Struct(v_other, wp_query, other);

  wp_query* result = wp_query_new_conjunction();
  result = wp_query_add(result, wp_query_clone(query));
  result = wp_query_add(result, wp_query_clone(other));

  VALUE o_result = Data_Wrap_Struct(c_query, NULL, wp_query_free, result);
  VALUE argv[2] = { Qnil, Qnil }; // i guess
  rb_obj_call_init(o_result, 2, argv);

  return o_result;
}

/*
 * call-seq: or(other)
 *
 * Returns a new Query that is a disjunction of this query and +other+, which
 * must also be a Query object.
 *
 */
static VALUE query_or(VALUE self, VALUE v_other) {
  if(CLASS_OF(v_other) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_query* query; Data_Get_Struct(self, wp_query, query);
  wp_query* other; Data_Get_Struct(v_other, wp_query, other);

  wp_query* result = wp_query_new_disjunction();
  result = wp_query_add(result, wp_query_clone(query));
  result = wp_query_add(result, wp_query_clone(other));

  VALUE o_result = Data_Wrap_Struct(c_query, NULL, wp_query_free, result);
  VALUE argv[2] = { Qnil, Qnil }; // i guess
  rb_obj_call_init(o_result, 2, argv);

  return o_result;
}

static VALUE query_init(VALUE self, VALUE query) {
  rb_iv_set(self, "@query", query);
  return self;
}

/*
 * call-seq: setup_query(query)
 *
 * Initializes query for use with run_query. If you do not call teardown_query
 * on this query later, you will leak memory.
 */
static VALUE index_setup_query(VALUE self, VALUE v_query) {
  if(CLASS_OF(v_query) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_query* query; Data_Get_Struct(v_query, wp_query, query);
  wp_error* e = wp_index_setup_query(index, query);
  RAISE_IF_NECESSARY(e);

  return self;
}

/*
 * call-seq: teardown_query(query)
 *
 * Releases any held state used by the query, if it has been first passed to
 * setup_query. If you call run_query on this query after calling this
 * function, terrible things will happen.
 */
static VALUE index_teardown_query(VALUE self, VALUE v_query) {
  if(CLASS_OF(v_query) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_query* query; Data_Get_Struct(v_query, wp_query, query);
  wp_error* e = wp_index_teardown_query(index, query);
  RAISE_IF_NECESSARY(e);

  return self;
}

/*
 * call-seq: run_query(query, max_num_results)
 *
 * Runs a query which has been first passed to setup_query, and returns an
 * array of at most +max_num_results+ doc ids. Can be called
 * multiple times to retrieve successive results from the query. The query
 * must have been passed to setup_query first, or terrible things will happen.
 * The query must be passed to teardown_query when done, or memory leaks will
 * occur.
 *
 */
static VALUE index_run_query(VALUE self, VALUE v_query, VALUE v_max_num_results) {
  Check_Type(v_max_num_results, T_FIXNUM);
  if(CLASS_OF(v_query) != c_query) {
    rb_raise(rb_eTypeError, "query must be a Whistlepig::Query object"); // would be nice to support subclasses somehow...
    // not reached
  }

  wp_index* index; Data_Get_Struct(self, wp_index, index);
  wp_query* query; Data_Get_Struct(v_query, wp_query, query);

  uint32_t max_num_results = NUM2INT(v_max_num_results);
  uint32_t num_results;
  uint64_t* results = malloc(sizeof(uint64_t) * max_num_results);

  wp_error* e = wp_index_run_query(index, query, max_num_results, &num_results, results);
  RAISE_IF_NECESSARY(e);

  VALUE array = rb_ary_new2(num_results);
  for(uint32_t i = 0; i < num_results; i++) {
    rb_ary_store(array, i, INT2NUM(results[i]));
  }
  free(results);

  return array;
}

void Init_whistlepigc() {
  VALUE m_whistlepig;

  m_whistlepig = rb_define_module("Whistlepig");

  c_index = rb_define_class_under(m_whistlepig, "Index", rb_cObject);
  rb_define_singleton_method(c_index, "new", index_new, 1);
  rb_define_singleton_method(c_index, "create", index_create, 1);
  rb_define_singleton_method(c_index, "load", index_load, 1);
  rb_define_singleton_method(c_index, "delete!", index_delete, 1);
  rb_define_singleton_method(c_index, "exists?", index_exists, 1);
  rb_define_method(c_index, "initialize", index_init, 1);
  rb_define_method(c_index, "close", index_close, 0);
  rb_define_method(c_index, "size", index_size, 0);
  rb_define_method(c_index, "add_entry", index_add_entry, 1);
  rb_define_method(c_index, "add_label", index_add_label, 2);
  rb_define_method(c_index, "remove_label", index_remove_label, 2);
  rb_define_method(c_index, "count", index_count, 1);
  rb_define_method(c_index, "setup_query", index_setup_query, 1);
  rb_define_method(c_index, "run_query", index_run_query, 2);
  rb_define_method(c_index, "teardown_query", index_teardown_query, 1);
  rb_define_attr(c_index, "pathname_base", 1, 0);

  c_entry = rb_define_class_under(m_whistlepig, "Entry", rb_cObject);
  rb_define_singleton_method(c_entry, "new", entry_new, 0);
  rb_define_method(c_entry, "size", entry_size, 0);
  rb_define_method(c_entry, "add_token", entry_add_token, 2);
  rb_define_method(c_entry, "add_string", entry_add_string, 2);
  //rb_define_method(c_entry, "add_file", entry_add_file, 2);

  c_query = rb_define_class_under(m_whistlepig, "Query", rb_cObject);
  rb_define_singleton_method(c_query, "new", query_new, 2);
  rb_define_method(c_query, "initialize", query_init, 2);
  rb_define_method(c_query, "and", query_and, 1);
  rb_define_method(c_query, "or", query_or, 1);
  rb_define_method(c_query, "to_s", query_to_s, 0);
  rb_define_attr(c_query, "query", 1, 0);

  c_error = rb_define_class_under(m_whistlepig, "Error", rb_eStandardError);
  c_parseerror = rb_define_class_under(m_whistlepig, "ParseError", rb_eStandardError);
}
