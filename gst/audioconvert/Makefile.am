plugin_LTLIBRARIES = libgstaudioconvert.la

libgstaudioconvert_la_SOURCES = \
	gstaudioconvert.c \
	plugin.c

libgstaudioconvert_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudioconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioconvert_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstaudioconvert_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioconvert.h \
	plugin.h
