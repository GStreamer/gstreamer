
plugin_LTLIBRARIES = libgstaudiofile.la

libgstaudiofile_la_SOURCES = gstaf.c gstafsink.c gstafsrc.c gstafparse.c
libgstaudiofile_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libgstaudiofile_la_LIBADD = $(AUDIOFILE_LIBS)
libgstaudiofile_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiofile_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstafsink.h gstafsrc.h  gstafparse.h
