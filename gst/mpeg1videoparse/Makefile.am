plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmp1videoparse.la

libgstmp1videoparse_la_SOURCES = gstmp1videoparse.c
libgstmp1videoparse_la_CFLAGS = $(GST_CFLAGS) -O3 $(FOMIT_FRAME_POINTER) $(GST_FFASTMATH_CFLAGS)
libgstmp1videoparse_la_LIBADD =
libgstmp1videoparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmp1videoparse.h
