
noinst_PROGRAMS = test1 test2

test1_SOURCES = test1.c gstbuffer.c gstmempool.c

BUILT_SOURCES = gstbuffer.c

gstbuffer.c:
	cp $(top_srcdir)/gst/gstbuffer.c .

EXTRA_DIST = .h gstmempool.h

LDADD = $(GST_LIBS)
AM_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst
