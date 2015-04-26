plugin_LTLIBRARIES = libgstadder.la

ORC_SOURCE=gstadderorc
include $(top_srcdir)/common/orc.mak


libgstadder_la_SOURCES = gstadder.c
nodist_libgstadder_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstadder_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstadder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstadder_la_LIBADD =  \
	         $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
		 $(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS)
libgstadder_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstadder.h
