
plugin_LTLIBRARIES = libgstinterleave.la

libgstinterleave_la_SOURCES = plugin.c interleave.c deinterleave.c
libgstinterleave_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstinterleave_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS)
libgstinterleave_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstinterleave_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = plugin.h interleave.h deinterleave.h
