plugin_LTLIBRARIES = libgstavi.la

libgstavi_la_SOURCES = \
	gstavi.c \
	gstavidemux.c

noinst_HEADERS = \
	avi-ids.h \
	gstavimux.h \
	gstavidemux.h

libgstavi_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_LIBS) \
	-DGETTEXT_PACKAGE="\"gst-plugins\""
libgstavi_la_LIBADD =
libgstavi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
	../../../gst-plugins-base/gst-libs/gst/riff/libgstriff-0.9.la

EXTRA_DIST = README gstavimux.c
