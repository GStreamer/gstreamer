plugin_LTLIBRARIES = libgstmpegtsmux.la

SUBDIRS = tsmux

libgstmpegtsmux_la_SOURCES = \
	mpegtsmux.c \
	mpegtsmux_h264.c \
	mpegtsmux_aac.c

libgstmpegtsmux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsmux_la_LIBADD = $(top_builddir)/gst/mpegtsmux/tsmux/libtsmux.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegtsmux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	mpegtsmux.h \
	mpegtsmux_h264.h \
	mpegtsmux_aac.h
