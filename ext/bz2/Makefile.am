
plugin_LTLIBRARIES = libgstbz2.la

libgstbz2_la_SOURCES = \
	gstbz2.c gstbz2dec.c gstbz2enc.c
libgstbz2_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstbz2_la_LIBADD = $(GST_BASE_LIBS) $(BZ2_LIBS) 
libgstbz2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbz2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstbz2dec.h gstbz2enc.h
