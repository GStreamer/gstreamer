plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlame.c

libgstlame_la_LIBADD = $(LAME_LIBS)
libgstlame_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstlame.h
