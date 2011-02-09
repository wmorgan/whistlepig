require 'mkmf'

$CFLAGS = "-g -O3 -std=c99 $(cflags)"

create_header
create_makefile "whistlepigc"
