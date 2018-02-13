plugin_LTLIBRARIES = libgstaudiomixer.la

ORC_SOURCE=gstaudiomixerorc
include $(top_srcdir)/common/orc.mak


libgstaudiomixer_la_SOURCES = gstaudiomixer.c gstaudiointerleave.c
nodist_libgstaudiomixer_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstaudiomixer_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(ORC_CFLAGS)
libgstaudiomixer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiomixer_la_LIBADD =  \
		$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
		$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS)

noinst_HEADERS = gstaudiomixer.h gstaudiointerleave.h

