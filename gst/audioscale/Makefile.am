
filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstaudioscale.la

libgstaudioscale_la_SOURCES = gstaudioscale.c
libgstaudioscale_la_LIBADD = $(top_builddir)/libs/resample/libresample.la
libgstaudioscale_la_CFLAGS = -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstaudioscale.h


