filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstgetbits.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstgetbits_i386.s
else
GSTARCH_SRCS = 
endif

libgstgetbits_la_SOURCES = gstgetbits.c gstgetbits_inl.h gstgetbits_generic.c $(GSTARCH_SRCS)
EXTRA_libgstgetbits_la_SOURCES = gstgetbits_i386.s

libgstgetbitsincludedir = $(includedir)/gst/libs/gstgetbits
libgstgetbitsinclude_HEADERS = gstgetbits.h

noinst_HEADERS = gstgetbits.h gstgetbits_inl.h

check_PROGRAMS = gbtest

gbtest_SOURCES = gbtest.c
gbtest_LDADD = libgstgetbits.la $(GST_LIBS)

libgstgetbits_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GLIB_CFLAGS) $(XML_CFLAGS)
