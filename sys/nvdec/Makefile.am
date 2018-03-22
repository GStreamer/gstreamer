plugin_LTLIBRARIES = libgstnvdec.la

libgstnvdec_la_SOURCES = \
	gstnvdec.c \
	plugin.c

if USE_DYNLINK_NVDEC
libgstnvdec_la_SOURCES += dynlink_nvcuvid.c
libgstnvdec_la_SOURCES += dynlink_cuda.c
endif

noinst_HEADERS = \
	gstnvdec.h

libgstnvdec_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_GL_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(GST_CFLAGS) \
	$(CUDA_CFLAGS)

libgstnvdec_la_LIBADD = \
	$(GST_GL_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(GST_LIBS)

if !USE_DYNLINK_NVDEC
libgstnvdec_la_LIBADD += $(CUDA_LIBS) -lnvcuvid
endif

libgstnvdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
