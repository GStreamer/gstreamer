##libdir = $(libdir)/gst

lib_LTLIBRARIES = libgstaudioscale.la

libgstaudioscale_la_SOURCES = gstaudioscale.c
libgstaudioscale_la_LIBADD = $(top_builddir)/gst-libs/gst/resample/libgstresample.la
libgstaudioscale_la_CFLAGS = -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstaudioscale.h


