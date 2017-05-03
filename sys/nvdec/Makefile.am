plugin_LTLIBRARIES = libgstnvdec.la

libgstnvdec_la_SOURCES = \
	gstnvdec.c \
	plugin.c

noinst_HEADERS = \
	gstnvdec.h

libgstnvdec_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(CUDA_CFLAGS)

libgstnvdec_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(CUDA_LIBS) -lnvcuvid \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la

libgstnvdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
