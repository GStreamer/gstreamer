plugindir = $(libdir)/gst

plugin_LTLIBRARIES = \
		libgstcdxaparse.la

libgstcdxaparse_la_SOURCES = gstcdxaparse.c 

noinst_HEADERS = \
		gstcdxaparse.h

libgstcdxaparse_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstcdxaparse_la_LIBADD =
libgstcdxaparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
