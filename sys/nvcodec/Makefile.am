plugin_LTLIBRARIES = libgstnvcodec.la

libgstnvcodec_la_SOURCES = \
	plugin.c \
	gstnvenc.c \
	gstnvbaseenc.c \
	gstnvh264enc.c \
	gstnvh265enc.c \
	gstcudaloader.c

if USE_NVCODEC_GST_GL
libgstnvcodec_la_SOURCES += \
	gstnvdec.c \
	gstcuvidloader.c
endif

noinst_HEADERS = \
	gstnvdec.h \
	gstnvenc.h \
	gstcuvidloader.h \
	gstnvbaseenc.h \
	gstnvh264enc.h \
	gstnvh265enc.h \
	nvEncodeAPI.h \
	cuviddec.h \
	nvcuvid.h \
	gstcuvidloader.h

libgstnvcodec_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(GST_CFLAGS) \
	$(CUDA_CFLAGS)

libgstnvcodec_la_LIBADD = \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(GST_LIBS) \
	$(GMODULE_NO_EXPORT_LIBS)

if USE_NVCODEC_GST_GL
libgstnvcodec_la_CFLAGS += $(GST_GL_CFLAGS)
libgstnvcodec_la_LIBADD += $(GST_GL_LIBS)
endif
libgstnvcodec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
