plugin_LTLIBRARIES = libgstnvenc.la

libgstnvenc_la_SOURCES = \
	gstnvenc.c \
	gstnvbaseenc.c \
	gstnvh264enc.c

noinst_HEADERS = \
	gstnvenc.h \
	gstnvbaseenc.h \
	gstnvh264enc.h

libgstnvenc_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(CUDA_CFLAGS) \
	$(NVENCODE_CFLAGS)

libgstnvenc_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(CUDA_LIBS) \
	$(NVENCODE_LIBS)

if USE_NVENC_GST_GL
libgstnvenc_la_LIBADD += \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la
endif
libgstnvenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
