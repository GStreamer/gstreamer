plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libvcdsrc.la

libvcdsrc_la_SOURCES = vcdsrc.c
libvcdsrc_la_CFLAGS = $(GST_CFLAGS)
libvcdsrc_la_LIBADD = $(GST_LIBS)
libvcdsrc_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = vcdsrc.h
