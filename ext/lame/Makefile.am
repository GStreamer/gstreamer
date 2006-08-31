plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlame.c
libgstlame_la_CFLAGS = $(GST_CFLAGS) $(LAME_CFLAGS)
libgstlame_la_LIBADD = $(LAME_LIBS) $(GST_LIBS)
libgstlame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlame.h
