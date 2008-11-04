plugin_LTLIBRARIES = libgstpolypaudio.la

libgstpolypaudio_la_SOURCES = plugin.c polypsink.c
libgstpolypaudio_la_CFLAGS = $(GST_CFLAGS)
libgstpolypaudio_la_LIBADD = $(GST_LIBS) $(POLYP_LIBS)
libgstpolypaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpolypaudio_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = polypsink.h
