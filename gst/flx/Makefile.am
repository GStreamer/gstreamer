plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstflxdec.la

libgstflxdec_la_SOURCES = gstflxdec.c flx_color.c
libgstflxdec_la_CFLAGS = $(GST_CFLAGS)
libgstflxdec_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = flx_fmt.h flx_color.h gstflxdec.h

