plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstudp.la

libgstudp_la_SOURCES = gstudp.c gstudpsrc.c gstudpsink.c
libgstudp_la_CFLAGS = $(GST_CFLAGS)
libgstudp_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstudpsink.h gstudpsrc.h 

EXTRA_DIST = README
