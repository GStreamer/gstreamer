plugin_LTLIBRARIES = libgstavi.la

libgstavi_la_SOURCES = \
	gstavi.c \
	gstavidemux.c \
	gstavimux.c 

noinst_HEADERS = \
	avi-ids.h \
	gstavimux.h \
	gstavidemux.h

libgstavi_la_CFLAGS = $(GST_CFLAGS)
libgstavi_la_LIBADD =
libgstavi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = README
