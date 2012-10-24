plugin_LTLIBRARIES = libgstassrender.la

libgstassrender_la_SOURCES = gstassrender.c
libgstassrender_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ASSRENDER_CFLAGS)
libgstassrender_la_LIBADD = $(ASSRENDER_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_API_VERSION@
libgstassrender_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstassrender_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstassrender.h
