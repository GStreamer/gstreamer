plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcdparanoia.la

libgstcdparanoia_la_SOURCES = gstcdparanoia.c
libgstcdparanoia_la_LIBADD = $(CDPARANOIA_LIBS)
libgstcdparanoia_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstcdparanoia.h
