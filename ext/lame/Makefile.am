plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlame.c gstlamemp3enc.c plugin.c
libgstlame_la_CFLAGS = $(GST_CFLAGS) $(LAME_CFLAGS)
libgstlame_la_LIBADD = $(LAME_LIBS) $(GST_LIBS)
libgstlame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlame_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstlame.h gstlamemp3enc.h
