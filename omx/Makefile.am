plugin_LTLIBRARIES = libgstomx.la

if HAVE_VP8
VP8_C_FILES=gstomxvp8dec.c
VP8_H_FILES=gstomxvp8dec.h
endif

if HAVE_THEORA
THEORA_C_FILES=gstomxtheoradec.c
THEORA_H_FILES=gstomxtheoradec.h
endif

if HAVE_HEVC
H265_C_FILES = \
	gstomxh265dec.c \
	gstomxh265enc.c \
	gstomxh265utils.c
H265_H_FILES = \
	gstomxh265dec.h \
	gstomxh265enc.h \
	gstomxh265utils.h
endif

libgstomx_la_SOURCES = \
	gstomx.c \
	gstomxbufferpool.c \
	gstomxvideo.c \
	gstomxvideodec.c \
	gstomxvideoenc.c \
	gstomxaudiodec.c \
	gstomxaudioenc.c \
	gstomxmjpegdec.c \
	gstomxmpeg4videodec.c \
	gstomxmpeg2videodec.c \
	gstomxh264dec.c \
	gstomxh264utils.c \
	gstomxh263dec.c \
	gstomxwmvdec.c \
	$(VP8_C_FILES) \
	$(THEORA_C_FILES) \
	$(H265_C_FILES) \
	gstomxmpeg4videoenc.c \
	gstomxh264enc.c \
	gstomxh263enc.c \
	gstomxaacdec.c \
	gstomxmp3dec.c \
	gstomxmp3enc.c \
	gstomxaacenc.c \
	gstomxamrdec.c \
	gstomxaudiosink.c \
	gstomxanalogaudiosink.c \
	gstomxhdmiaudiosink.c	

noinst_HEADERS = \
	gstomx.h \
	gstomxbufferpool.h \
	gstomxvideo.h \
	gstomxvideodec.h \
	gstomxvideoenc.h \
	gstomxaudiodec.h \
	gstomxaudioenc.h \
	gstomxmjpegdec.h \
	gstomxmpeg2videodec.h \
	gstomxmpeg4videodec.h \
	gstomxh264dec.h \
	gstomxh264utils.h \
	gstomxh263dec.h \
	gstomxwmvdec.h \
	$(VP8_H_FILES) \
	$(THEORA_H_FILES) \
	$(H265_H_FILES) \
	gstomxmpeg4videoenc.h \
	gstomxh264enc.h \
	gstomxh263enc.h \
	gstomxaacdec.h \
	gstomxmp3dec.h \
	gstomxmp3enc.h \
	gstomxaacenc.h \
	gstomxamrdec.h \
	gstomxaudiosink.h \
	gstomxanalogaudiosink.h \
	gstomxhdmiaudiosink.h 	

if !HAVE_EXTERNAL_OMX
OMX_INCLUDEPATH = -I$(abs_srcdir)/openmax
endif

libgstomx_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API=1 \
	$(OMX_INCLUDEPATH) \
	$(GST_GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_ALLOCATORS_CFLAGS) \
	$(GMODULE_NO_EXPORT_CFLAGS)
libgstomx_la_LIBADD = \
	$(GST_GL_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_ALLOCATORS_LIBS) \
	$(GMODULE_NO_EXPORT_LIBS)
libgstomx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = \
	openmax \
	gstomxvp8dec.c \
	gstomxvp8dec.h \
	gstomxtheoradec.c \
	gstomxtheoradec.h
