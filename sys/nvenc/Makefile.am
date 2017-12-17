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
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(GST_CFLAGS) \
	$(CUDA_CFLAGS) \
	$(NVENCODE_CFLAGS)

libgstnvenc_la_LIBADD = \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(GST_LIBS) \
	$(CUDA_LIBS) \
	$(NVENCODE_LIBS)

if USE_NVENC_GST_GL
libgstnvenc_la_CFLAGS += $(GST_GL_CFLAGS)
libgstnvenc_la_LIBADD += $(GST_GL_LIBS)
endif
libgstnvenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
