librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstputbits.la

libgstputbitsincludedir = $(includedir)/gst/putbits
libgstputbitsinclude_HEADERS = putbits.h

libgstputbits_la_SOURCES = putbits.c 
libgstputbits_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
libgstputbits_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = putbits.h 
