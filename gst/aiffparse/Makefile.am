plugin_LTLIBRARIES = libgstaiffparse.la

libgstaiffparse_la_SOURCES = aiffparse.c
libgstaiffparse_la_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaiffparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstaiffparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = aiffparse.h
