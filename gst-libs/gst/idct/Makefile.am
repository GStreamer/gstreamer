
librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstidct.la

libgstidct_la_SOURCES = \
        fastintidct.c \
	floatidct.c \
	idct.c \
	intidct.c

libgstidctincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/idct
libgstidctinclude_HEADERS = idct.h
noinst_HEADERS = dct.h

## check_PROGRAMS = ieeetest

## ieeetest_SOURCES = ieeetest.c
## ieeetest_LDADD = libgstidct.la
## ieeetest_CFLAGS = $(GST_CFLAGS)
## ieeetest_LDFLAGS = $(GST_LIBS)


libgstidct_la_LIBADD =
libgstidct_la_CFLAGS = $(GST_CFLAGS)
libgstidct_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
