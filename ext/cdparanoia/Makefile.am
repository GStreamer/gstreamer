plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcdparanoia.la

libgstcdparanoia_la_SOURCES = gstcdparanoia.c
libgstcdparanoia_la_CFLAGS = $(GST_CFLAGS)
libgstcdparanoia_la_LIBADD = $(GST_LIBS) $(CDPARANOIA_LIBS)

noinst_HEADERS = gstcdparanoia.h
