filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstputbits.la

libgstputbits_la_SOURCES = gstputbits.c 

libgstputbitsincludedir = $(includedir)/gst/libs/gstputbits
libgstputbitsinclude_HEADERS = gstputbits.h

noinst_HEADERS = gstputbits.h 

libgstputbits_la_LIBADD = $(GST_LIBS)
libgstputbits_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
