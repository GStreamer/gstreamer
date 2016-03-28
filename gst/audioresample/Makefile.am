plugin_LTLIBRARIES = libgstaudioresample.la

libgstaudioresample_la_SOURCES = \
	gstaudioresample.c 

nodist_libgstaudioresample_la_SOURCES = $(BUILT_SOURCES)

libgstaudioresample_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstaudioresample_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)

libgstaudioresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioresample_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioresample.h
