plugin_LTLIBRARIES = libgstvolume.la

ORC_SOURCE=gstvolumeorc
include $(top_srcdir)/common/orc.mak

libgstvolume_la_SOURCES = gstvolume.c
nodist_libgstvolume_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstvolume_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvolume_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la  \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstvolume_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvolume.h
