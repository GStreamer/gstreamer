plugin_LTLIBRARIES = libgsttwolame.la

libgsttwolame_la_SOURCES = gsttwolame.c
libgsttwolame_la_CFLAGS = $(GST_CFLAGS) $(TWOLAME_CFLAGS)
libgsttwolame_la_LIBADD = $(TWOLAME_LIBS) $(GST_LIBS)
libgsttwolame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttwolame_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsttwolame.h
