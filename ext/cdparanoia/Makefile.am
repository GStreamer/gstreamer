plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcdparanoia.la

libgstcdparanoia_la_SOURCES = gstcdparanoia.c
libgstcdparanoia_la_CFLAGS = $(GST_CFLAGS)
libgstcdparanoia_la_LIBADD = $(CDPARANOIA_LIBS)
libgstcdparanoia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcdparanoia.h
