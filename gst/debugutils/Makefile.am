plugin_LTLIBRARIES = libgstfpsdisplaysink.la

libgstfpsdisplaysink_la_SOURCES = fpsdisplaysink.c
libgstfpsdisplaysink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstfpsdisplaysink_la_LIBADD = $(GST_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
libgstfpsdisplaysink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfpsdisplaysink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = fpsdisplaysink.h

