plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstswfdec.la

libgstswfdec_la_SOURCES = gstswfdec.c
libgstswfdec_la_CFLAGS = $(GST_CFLAGS) $(SWFDEC_CFLAGS)
libgstswfdec_la_LIBADD = $(SWFDEC_LIBS)
libgstswfdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstswfdec.h

