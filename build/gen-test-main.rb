#!/usr/bin/env ruby

abort "expecting one argument: the filename" unless ARGV.size == 1
fn = ARGV.shift
funcs = []
IO.foreach(fn) do |l|
  if l =~ /TEST\((.+?)\)/
    funcs << $1
  end
end

puts %q!
#include <stdio.h>
#include "error.h"
#include "test.h"
!

puts funcs.map { |f| "TEST(#{f});" }

puts %q!
int main(int argc, char* argv[]) {
  (void) argc; (void) argv;
  int failures = 0, errors = 0, asserts = 0, tests = 0;

  //printf("Running tests...\n\n");

!
puts funcs.map { |f| "RUNTEST(#{f});" }

puts %q!
  printf("%d tests, %d assertions, %d failures, %d errors\n", tests, asserts, failures, errors);

  if((errors == 0) && (failures == 0)) {
   // printf("Tests passed.\n");
    return 0;
  }

  else {
    //printf("Tests FAILED.\n");
    return -1;
  }
}
!

