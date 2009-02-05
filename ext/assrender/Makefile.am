plugin_LTLIBRARIES = libgstassrender.la

libgstassrender_la_SOURCES = gstassrender.c
libgstassrender_la_CFLAGS = $(GST_CFLAGS) $(ASSRENDER_CFLAGS)
libgstassrender_la_LIBADD = $(ASSRENDER_LIBS) $(GST_LIBS)
libgstassrender_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstassrender_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstassrender.h
