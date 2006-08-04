plugin_LTLIBRARIES = libgstrmdemux.la

libgstrmdemux_la_SOURCES = rmdemux.c rmutils.c

libgstrmdemux_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstrmdemux_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstrmdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = rmdemux.h rmutils.h
