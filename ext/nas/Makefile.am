plugin_LTLIBRARIES = libgstnassink.la

libgstnassink_la_SOURCES = nassink.c
libgstnassink_la_CFLAGS = $(GST_CFLAGS) $(NAS_CFLAGS)
libgstnassink_la_LIBADD = $(NAS_LIBS)
libgstnassink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = nassink.h
EXTRA_DIST = README
