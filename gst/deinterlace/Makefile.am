plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdeinterlace.la

libgstdeinterlace_la_SOURCES = gstdeinterlace.c
libgstdeinterlace_la_CFLAGS = $(GST_CFLAGS)
libgstdeinterlace_la_LIBADD = $(GST_LIBS)
libgstdeinterlace_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstdeinterlace.h 

