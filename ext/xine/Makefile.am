plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstxine.la

libgstxine_la_SOURCES = xine.c xinecaps.c xineaudiodec.c
libgstxine_la_CFLAGS = $(GST_CFLAGS) $(XINE_CFLAGS)
libgstxine_la_LIBADD = $(XINE_LIBS)
libgstxine_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstxine.h

