librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstputbits.la

libgstputbitsincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/putbits
libgstputbitsinclude_HEADERS = putbits.h

libgstputbits_la_SOURCES = putbits.c 
libgstputbits_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
libgstputbits_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
