plugin_LTLIBRARIES = libgstgetbits.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstgetbits_i386.s
else
GSTARCH_SRCS = 
endif

libgstgetbitsincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/getbits
libgstgetbitsinclude_HEADERS = getbits.h
noinst_HEADERS = gstgetbits_inl.h

libgstgetbits_la_SOURCES = getbits.c gstgetbits_inl.h gstgetbits_generic.c $(GSTARCH_SRCS)
EXTRA_libgstgetbits_la_SOURCES = gstgetbits_i386.s

libgstgetbits_la_CFLAGS = $(GST_OBJ_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
libgstgetbits_la_LDFLAGS = $(GST_OBJ_LIBS) $(GST_PLUGIN_LDFLAGS)

# check_PROGRAMS = gbtest

