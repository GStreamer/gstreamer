
plugin_LTLIBRARIES = libgstsmooth.la

libgstsmooth_la_SOURCES = gstsmooth.c
libgstsmooth_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_FUNROLL_CFLAGS) $(GST_CFLAGS)
libgstsmooth_la_LIBADD =
libgstsmooth_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsmooth.h
