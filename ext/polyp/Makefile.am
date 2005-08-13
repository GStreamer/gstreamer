plugin_LTLIBRARIES = libgstpolypaudio.la

libpolypaudio_la_SOURCES = plugin.c polypsink.c
libpolypaudio_la_CFLAGS = $(GST_CFLAGS)
libpolypaudio_la_LIBADD = $(GST_LIBS) $(POLYP_LIBS)
libpolypaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = polypsink.h
