#ifndef WP_TEST_H_
#define WP_TEST_H_

#include <inttypes.h>

// whistlepig test header file
// (c) 2011 William Morgan. See COPYING for license terms.
//
// macros for the c unit tests

#define ASSERT(x) do { \
  (*asserts)++; \
  if(!(x)) { \
    printf("-- test failure: (" #x ") is FALSE in %s (%s:%d)\n", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    *fail = 1; \
    return NO_ERROR; \
  } \
} while(0)

#define ASSERT_EQUALS_UINT(truth, val) do { \
  (*asserts)++; \
  if(val != truth) { \
    printf("-- test failure: " #val " == %u but should be %u in %s (%s:%d)\n", val, truth, __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    *fail = 1; \
    return NO_ERROR; \
  } \
} while(0)

#define ASSERT_EQUALS_UINT64(truth, val) do { \
  (*asserts)++; \
  if(val != truth) { \
    printf("-- test failure: " #val " == %" PRIu64 " but should be %u in %s (%s:%d)\n", val, truth, __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    *fail = 1; \
    return NO_ERROR; \
  } \
} while(0)

#define ASSERT_EQUALS_PTR(truth, val) do { \
  (*asserts)++; \
  if(val != truth) { \
    printf("-- test failure: " #val " == %p but should be %p in %s (%s:%d)\n", val, truth, __PRETTY_FUNCTION__, __FILE__, __LINE__); \
    *fail = 1; \
    return NO_ERROR; \
  } \
} while(0)

#define TEST(x) wp_error* test_##x(int* fail, int* asserts)

#define RUNTEST(x) do { \
  int fail = 0; \
  int this_asserts = 0; \
  tests++; \
  wp_error* err = test_##x(&fail, &this_asserts); \
  asserts += this_asserts; \
  if(fail) { \
    printf("FAIL " #x "\n"); \
    failures++; \
  } \
  else if(err) { \
    errors++; \
    printf(" ERR " #x "\n"); \
    PRINT_ERROR(err, stdout); \
  } \
  else printf("PASS %d/%d " #x "\n", this_asserts, this_asserts); \
} while(0)

#endif
