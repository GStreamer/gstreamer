plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlame.c
libgstlame_la_CFLAGS = $(GST_CFLAGS)
libgstlame_la_LIBADD = $(GST_LIBS) $(LAME_LIBS)
libgstlame_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstlame.h
