plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmedian.la

libgstmedian_la_SOURCES = gstmedian.c

libgstmedian_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_FUNROLL_CFLAGS) $(GST_CFLAGS)
libgstmedian_la_LIBADD =
libgstmedian_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmedian.h
