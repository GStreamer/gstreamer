plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsynaesthesia.la

libgstsynaesthesia_la_SOURCES = gstsynaesthesia.c synaescope.c

noinst_HEADERS = synaescope.h

libgstsynaesthesia_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS)
libgstsynaesthesia_la_LIBADD = $(GST_LIBS)
libgstsynaesthesia_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

