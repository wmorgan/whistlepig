#ifndef WP_TEST_H_
#define WP_TEST_H_

// whistlepig test header file
// (c) 2011 William Morgan. See COPYING for license terms.
//
// macros for the c unit tests

#define ASSERT(x) do { \
  (*asserts)++; \
  if(!(x)) { \
    printf("-- test failure: (" #x ") is FALSE in %s (%s:%d)\n\n", __PRETTY_FUNCTION__, __FILE__, __LINE__); \
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
