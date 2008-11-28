plugin_LTLIBRARIES = libgstmxf.la

libgstmxf_la_SOURCES = \
	mxf.c \
	mxfdemux.c \
	mxfparse.c \
	mxfaes-bwf.c \
	mxfmpeg.c \
	mxfdv-dif.c \
	mxfalaw.c

libgstmxf_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstmxf_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstmxf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mxfdemux.h \
	mxfparse.h \
	mxfaes-bwf.h \
	mxfmpeg.h \
	mxfdv-dif.h \
	mxfalaw.h \
	mxftypes.h

