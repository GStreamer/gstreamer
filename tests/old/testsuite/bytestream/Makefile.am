## filterdir = $(libdir)/gst

testprogs = test1 

check_PROGRAMS = $(testprogs)

test1_SOURCES = test1.c gstbstest.c mem.c
test1_CFLAGS = $(GST_CFLAGS)
test1_LDFLAGS = $(GST_LIBS) -L$(top_builddir)/libs/gst/bytestream/ -lgstbytestream

noinst_HEADERS = mem.h
