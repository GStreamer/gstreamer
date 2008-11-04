
plugin_LTLIBRARIES = libgstdvdspu.la

libgstdvdspu_la_SOURCES = gstdvdspu.c gstdvdspu-render.c

libgstdvdspu_la_CFLAGS = $(GST_CFLAGS)
libgstdvdspu_la_LIBADD = $(GST_LIBS)
libgstdvdspu_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdspu_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdvdspu.h

EXTRA_DIST = Notes.txt
