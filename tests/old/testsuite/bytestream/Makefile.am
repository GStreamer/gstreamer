filterdir = $(libdir)/gst

testprogs = test1 

check_PROGRAMS = $(testprogs)

test1_SOURCES = test1.c gstbstest.c mem.c
test1_HEADERS = mem.h
test1_LDFLAGS = -L$(top_builddir)/libs/bytestream/ -lgstbytestream


# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
