plugin_LTLIBRARIES = libgstgetbits.la
AS_LIBTOOL_LIB = libgstgstgetbits

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

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
libgstgetbits_la_LDFLAGS = $(GST_OBJ_LIBS) $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

# check_PROGRAMS = gbtest

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak
