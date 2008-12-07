plugin_LTLIBRARIES = libgstmxf.la

libgstmxf_la_SOURCES = \
	mxf.c \
	mxfdemux.c \
	mxfparse.c \
	mxfaes-bwf.c \
	mxfmpeg.c \
	mxfdv-dif.c \
	mxfalaw.c \
	mxfjpeg2000.c \
	mxfd10.c \
	mxfup.c

libgstmxf_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstmxf_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@
libgstmxf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mxfdemux.h \
	mxfparse.h \
	mxfaes-bwf.h \
	mxfmpeg.h \
	mxfdv-dif.h \
	mxfalaw.h \
	mxfjpeg2000.h \
	mxfd10.h \
	mxfup.h \
	mxftypes.h

