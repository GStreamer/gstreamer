plugin_LTLIBRARIES = libgstcacasink.la

libgstcacasink_la_SOURCES = gstcacasink.c
libgstcacasink_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(LIBCACA_CFLAGS)
libgstcacasink_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBCACA_LIBS)
libgstcacasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcacasink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcacasink.h
