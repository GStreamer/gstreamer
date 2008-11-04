
plugin_LTLIBRARIES = libgstmultifile.la

libgstmultifile_la_SOURCES = gstmultifilesink.c gstmultifilesrc.c gstmultifile.c
libgstmultifile_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmultifile_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmultifile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmultifile_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmultifilesrc.h gstmultifilesink.h

