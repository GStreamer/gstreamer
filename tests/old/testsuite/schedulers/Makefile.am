include ../Rules

tests_pass = \
	unlink_src unlink_sink \
	relink_src relink_sink \
	unref_src unref_sink \
	143777 143777-2 147713 

# don't enable this one unless it actually works.
#	useless_iteration

tests_fail = 142183 142183-2 147894 147819
tests_ignore =

unlink_src_SOURCES = unlink.c
unlink_src_CFLAGS = $(AM_CFLAGS) -DELEMENT=src
unlink_sink_SOURCES = unlink.c
unlink_sink_CFLAGS = $(AM_CFLAGS) -DELEMENT=sink
relink_src_SOURCES = relink.c
relink_src_CFLAGS = $(AM_CFLAGS) -DELEMENT=src -DOTHER_ELEMENT=sink
relink_sink_SOURCES = relink.c
relink_sink_CFLAGS = $(AM_CFLAGS) -DELEMENT=sink -DOTHER_ELEMENT=src
unref_src_SOURCES = unref.c
unref_src_CFLAGS = $(AM_CFLAGS) -DELEMENT=src
unref_sink_SOURCES = unref.c
unref_sink_CFLAGS = $(AM_CFLAGS) -DELEMENT=sink
