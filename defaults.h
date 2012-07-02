#ifndef WP_DEFAULTS_H_
#define WP_DEFAULTS_H_

// whistlepig defaults
// (c) 2011 William Morgan. See COPYING for license terms.
//
// just some generic definitions that we use in many places.

#include <stdint.h>

// these two types are segment-specific. an index as a whole uses a larger
// datatype for docids, and doesn't do anything with positions. but we
// refer to them all over the place, so it's convenient to break them out
// here rather than in segment.h.
typedef uint32_t docid_t;
typedef uint32_t pos_t; // position of a term within a document

#define OFFSET_NONE (uint32_t)0
#define DOCID_NONE (docid_t)0

// if you define DEBUGOUTPUT, all the DEBUG statements will magically start
// printing stuff out...
#ifdef DEBUGOUTPUT
#define DEBUG(fmt, ...) do { \
  fprintf(stdout, "DEBUG %s:%d (%s): " fmt "\n", __FILE__, __LINE__, __PRETTY_FUNCTION__, ## __VA_ARGS__); \
} while(0)
#else
#define DEBUG(fmt, ...) do { } while(0)
#endif

#endif
