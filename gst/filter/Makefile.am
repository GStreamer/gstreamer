plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstfilter.la

libgstfilter_la_SOURCES = gstfilter.c gstiir.c iir.c gstlpwsinc.c gstbpwsinc.c
libgstfilter_la_CFLAGS = $(GST_CFLAGS)
libgstfilter_la_LIBADD =
libgstfilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfilter.h iir.h
