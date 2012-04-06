#ifndef SNIPPETER_H_
#define SNIPPETER_H_

#include "error.h"
#include "query.h"

wp_error* wp_snippetize_string(wp_query* query, const char* field, const char* string, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets);
wp_error* wp_snippetize_file(wp_query* query, const char* field, FILE* f, uint32_t max_num_results, uint32_t* num_results, pos_t* start_offsets, pos_t* end_offsets);

#endif
