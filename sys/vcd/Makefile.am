plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstvcdsrc.la

libgstvcdsrc_la_SOURCES = vcdsrc.c
libgstvcdsrc_la_CFLAGS = $(GST_CFLAGS)
libgstvcdsrc_la_LIBADD = $(GST_LIBS)
libgstvcdsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = vcdsrc.h
