plugin_LTLIBRARIES = libgstavi.la

libgstavi_la_SOURCES = \
	gstavi.c \
	gstavidemux.c

noinst_HEADERS = \
	avi-ids.h \
	gstavimux.h \
	gstavidemux.h

libgstavi_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-I$(top_srcdir)/gst-libs	\
	-DGETTEXT_PACKAGE="\"gst-plugins\""
libgstavi_la_LIBADD =
libgstavi_la_LDFLAGS = \
	$(GST_PLUGIN_LDFLAGS) \
	$(GST_PLUGINS_BASE_LIBS)

EXTRA_DIST = README gstavimux.c
