plugin_LTLIBRARIES = libgstdvdsub.la

libgstdvdsub_la_SOURCES = gstdvdsubdec.c gstdvdsubparse.c
libgstdvdsub_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdsub_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstdvdsub_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdsub_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdvdsubdec.h gstdvdsubparse.h
