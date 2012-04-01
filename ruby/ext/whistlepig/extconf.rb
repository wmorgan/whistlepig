require 'mkmf'

$CFLAGS= "-std=c99 -D_ANSI_SOURCE -D_XOPEN_SOURCE=600 $(cflags)"

create_header
create_makefile "whistlepig/whistlepig"
