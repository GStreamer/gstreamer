if HAVE_NASM
TEST1=test1
else
TEST1=
endif

noinst_PROGRAMS = $(TEST1) test2

test1_SOURCES = test1.c $(top_srcdir)/gst/gstbuffer.c gstmempool.c

EXTRA_DIST = gstbuffer.h gstmempool.h

LDADD = $(GST_LIBS)
CFLAGS = $(GST_CFLAGS) 
