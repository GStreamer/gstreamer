plugin_LTLIBRARIES = libgstsmpte.la

libgstsmpte_la_SOURCES = gstsmpte.c gstmask.c barboxwipes.c paint.c gstsmptealpha.c plugin.c

noinst_HEADERS = gstsmpte.h gstmask.h paint.h gstsmptealpha.h

libgstsmpte_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsmpte_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(LIBM) \
			-lgstvideo-$(GST_API_VERSION) 
libgstsmpte_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsmpte_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
