plugin_LTLIBRARIES = libgstdebugutilsbad.la

libgstdebugutilsbad_la_SOURCES = fpsdisplaysink.c debugutilsbad.c
libgstdebugutilsbad_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstdebugutilsbad_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
libgstdebugutilsbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdebugutilsbad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = fpsdisplaysink.h

