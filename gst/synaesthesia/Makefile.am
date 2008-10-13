plugin_LTLIBRARIES = libgstsynaesthesia.la

libgstsynaesthesia_la_SOURCES = gstsynaesthesia.c synaescope.c

noinst_HEADERS = synaescope.h gstsynaesthesia.h

libgstsynaesthesia_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstsynaesthesia_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstsynaesthesia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsynaesthesia_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = README
