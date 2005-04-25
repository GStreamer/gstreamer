
#plugin_LTLIBRARIES = libgstaudioscale.la
noinst_LTLIBRARIES = libgstresample.la

libgstaudioscale_la_SOURCES = gstaudioscale.c
libgstaudioscale_la_CFLAGS = $(GST_CFLAGS)
libgstaudioscale_la_LIBADD =
libgstaudioscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstaudioscale.h private.h

libgstresample_la_SOURCES = dtos.c dtof.c functable.c resample.c resample.h
libgstresample_la_LIBADD =
libgstresample_la_CFLAGS = $(GST_CFLAGS)
libgstresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


