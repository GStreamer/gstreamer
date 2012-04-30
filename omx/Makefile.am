plugin_LTLIBRARIES = libgstopenmax.la

libgstopenmax_la_SOURCES = \
	gstomx.c \
	gstomxvideodec.c \
	gstomxvideoenc.c \
	gstomxaudioenc.c \
	gstomxmpeg4videodec.c \
	gstomxh264dec.c \
	gstomxh263dec.c \
	gstomxwmvdec.c \
	gstomxmpeg4videoenc.c \
	gstomxh264enc.c \
	gstomxh263enc.c \
	gstomxaacenc.c \
	gstbasevideocodec.c \
	gstbasevideodecoder.c \
	gstbasevideoencoder.c \
	gstbasevideoutils.c \
	gstomxrecmutex.c

noinst_HEADERS = \
	gstomx.h \
	gstomxvideodec.h \
	gstomxvideoenc.h \
	gstomxaudioenc.h \
	gstomxmpeg4videodec.h \
	gstomxh264dec.h \
	gstomxh263dec.h \
	gstomxwmvdec.h \
	gstomxmpeg4videoenc.h \
	gstomxh264enc.h \
	gstomxh263enc.h \
	gstomxaacenc.h \
	gstbasevideocodec.h \
	gstbasevideodecoder.h \
	gstbasevideoencoder.h \
	gstbasevideoutils.h \
	gstomxrecmutex.h

fixbaseclasses = \
        -DGstBaseVideoCodec=OMXBaseVideoCodec \
        -DGstBaseVideoCodecClass=OMXBaseVideoCodecClass \
        -DGstBaseVideoEncoder=OMXBaseVideoEncoder \
        -DGstBaseVideoEncoderClass=OMXBaseVideoEncoderClass \
        -DGstBaseVideoDecoder=OMXBaseVideoDecoder \
        -DGstBaseVideoDecoderClass=OMXBaseVideoDecoderClass

libgstopenmax_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API=1 \
	-I$(abs_srcdir)/openmax \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(fixbaseclasses)
libgstopenmax_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstopenmax_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = openmax gstomx.conf

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstopenmax -:SHARED libgstopenmax \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstopenmax_la_SOURCES) \
	           $(nodist_libgstopenmax_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstopenmax_la_CFLAGS) \
	 -:LDFLAGS $(libgstopenmax_la_LDFLAGS) \
	           $(libgstopenmax_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
