librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstputbits.la

libgstputbits_la_SOURCES = putbits.c 

libgstputbitsincludedir = $(includedir)/gst/putbits
libgstputbitsinclude_HEADERS = putbits.h

noinst_HEADERS = putbits.h 

libgstputbits_la_LIBADD = $(GST_LIBS)
libgstputbits_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
