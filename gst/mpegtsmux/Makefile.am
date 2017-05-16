plugin_LTLIBRARIES = libgstmpegtsmux.la

SUBDIRS = tsmux

libgstmpegtsmux_la_SOURCES = \
	mpegtsmux.c \
	mpegtsmux_aac.c \
	mpegtsmux_ttxt.c \
	mpegtsmux_opus.c

libgstmpegtsmux_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			    $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsmux_la_LIBADD = $(top_builddir)/gst/mpegtsmux/tsmux/libtsmux.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ -lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mpegtsmux.h \
	mpegtsmux_aac.h \
	mpegtsmux_ttxt.h \
	mpegtsmux_opus.h
