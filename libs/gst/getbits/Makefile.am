librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstgetbits.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstgetbits_i386.s
else
GSTARCH_SRCS = 
endif

libgstgetbits_la_SOURCES = getbits.c gstgetbits_inl.h gstgetbits_generic.c $(GSTARCH_SRCS)
libgstgetbits_la_LIBADD = $(GST_LIBS)
libgstgetbits_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
EXTRA_libgstgetbits_la_SOURCES = gstgetbits_i386.s

libgstgetbitsincludedir = $(includedir)/gst/getbits
libgstgetbitsinclude_HEADERS = getbits.h

noinst_HEADERS = getbits.h gstgetbits_inl.h

# check_PROGRAMS = gbtest

