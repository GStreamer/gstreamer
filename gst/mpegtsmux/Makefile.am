plugin_LTLIBRARIES = libgstmpegtsmux.la

SUBDIRS = tsmux

libgstmpegtsmux_la_SOURCES = \
	mpegtsmux.c \
	mpegtsmux_aac.c \
	mpegtsmux_ttxt.c

libgstmpegtsmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			    $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsmux_la_LIBADD = $(top_builddir)/gst/mpegtsmux/tsmux/libtsmux.la \
	-lgsttag-@GST_API_VERSION@ \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegtsmux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	mpegtsmux.h \
	mpegtsmux_aac.h \
	mpegtsmux_ttxt.h
