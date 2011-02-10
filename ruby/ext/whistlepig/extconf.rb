require 'mkmf'

$CFLAGS = "-g -O3 -std=c99 $(cflags) -D_ANSI_SOURCE"

create_header
create_makefile "whistlepigc"
