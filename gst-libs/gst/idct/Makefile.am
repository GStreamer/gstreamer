if HAVE_LIBMMX
GSTIDCTARCH_SRCS = mmxidct.S mmx32idct.c sseidct.S
else
GSTIDCTARCH_SRCS =
endif

librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstidct.la

libgstidct_la_SOURCES = \
        fastintidct.c \
	floatidct.c \
	idct.c \
	intidct.c \
	$(GSTIDCTARCH_SRCS)

libgstidctincludedir = $(includedir)/gst/idct
libgstidctinclude_HEADERS = idct.h

check_PROGRAMS = ieeetest

ieeetest_SOURCES = ieeetest.c
ieeetest_LDADD = libgstidct.la
ieeetest_CFLAGS = $(GST_CFLAGS)
ieeetest_LDFLAGS = $(GST_LIBS)

noinst_HEADERS = dct.h

libgstidct_la_LIBADD =
libgstidct_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
libgstidct_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
