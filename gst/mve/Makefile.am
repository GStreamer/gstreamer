plugin_LTLIBRARIES = libgstmve.la

libgstmve_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmve_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstmve_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmve_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstmve_la_SOURCES = \
	gstmve.c \
	gstmvemux.c \
	gstmvedemux.c \
	mveaudiodec.c \
	mvevideodec8.c \
	mvevideodec16.c \
	mveaudioenc.c \
	mvevideoenc8.c \
	mvevideoenc16.c

noinst_HEADERS = gstmvedemux.h gstmvemux.h mve.h

EXTRA_DIST = TODO
