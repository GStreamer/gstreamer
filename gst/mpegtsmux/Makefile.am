plugin_LTLIBRARIES = libgstmpegtsmux.la

SUBDIRS = tsmux

libgstmpegtsmux_la_SOURCES = \
	mpegtsmux.c \
	mpegtsmux_h264.c \
	mpegtsmux_aac.c

libgstmpegtsmux_la_CFLAGS = $(GST_CFLAGS)
libgstmpegtsmux_la_LIBADD = $(top_builddir)/gst/mpegtsmux/tsmux/libtsmux.la $(GST_LIBS) $(GST_BASE_LIBS)
libgstmpegtsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mpegtsmux.h \
	mpegtsmux_h264.h \
	mpegtsmux_aac.h
