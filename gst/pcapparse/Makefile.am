plugin_LTLIBRARIES = libgstpcapparse.la

libgstpcapparse_la_SOURCES = \
	gstpcapparse.c

noinst_HEADERS = \
	gstpcapparse.h

libgstpcapparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstpcapparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstpcapparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpcapparse_la_LIBTOOLFLAGS = --tag=disable-static

