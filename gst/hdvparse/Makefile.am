plugin_LTLIBRARIES = libgsthdvparse.la

libgsthdvparse_la_SOURCES = \
	gsthdvparse.c

noinst_HEADERS = \
	gsthdvparse.h

libgsthdvparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgsthdvparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(LIBM)
libgsthdvparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsthdvparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
