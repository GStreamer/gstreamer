plugin_LTLIBRARIES = libgstmxf.la

libgstmxf_la_SOURCES = \
	mxf.c \
	mxful.c \
	mxftypes.c \
	mxfmetadata.c \
	mxfessence.c \
	mxfquark.c \
	mxfmux.c \
	mxfdemux.c \
	mxfaes-bwf.c \
	mxfmpeg.c \
	mxfdv-dif.c \
	mxfalaw.c \
	mxfjpeg2000.c \
	mxfd10.c \
	mxfup.c \
	mxfvc3.c

libgstmxf_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstmxf_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstmxf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mxful.h \
	mxftypes.h \
	mxfmetadata.h \
	mxfquark.h \
	mxfessence.h \
	mxfdemux.h \
	mxfmux.h \
	mxfaes-bwf.h \
	mxfmpeg.h \
	mxfdv-dif.h \
	mxfalaw.h \
	mxfjpeg2000.h \
	mxfd10.h \
	mxfup.h \
	mxfvc3.h
