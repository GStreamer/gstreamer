plugin_LTLIBRARIES = libgstomx.la

libgstomx_la_SOURCES = \
	gstomx.c \
	gstomxvideodec.c \
	gstomxvideoenc.c \
	gstomxmpeg4videodec.c \
	gstomxh264dec.c \
	gstomxmpeg4videoenc.c \
	gstbasevideocodec.c \
	gstbasevideodecoder.c \
	gstbasevideoencoder.c \
	gstbasevideoutils.c

noinst_HEADERS = \
	gstomx.h \
	gstomxvideodec.h \
	gstomxvideoenc.h \
	gstomxmpeg4videodec.h \
	gstomxh264dec.h \
	gstomxmpeg4videoenc.h \
	gstbasevideocodec.h \
	gstbasevideodecoder.h \
	gstbasevideoencoder.h \
	gstbasevideoutils.h

fixbaseclasses = \
        -DGstBaseVideoCodec=OMXBaseVideoCodec \
        -DGstBaseVideoCodecClass=OMXBaseVideoCodecClass \
        -DGstBaseVideoEncoder=OMXBaseVideoEncoder \
        -DGstBaseVideoEncoderClass=OMXBaseVideoEncoderClass \
        -DGstBaseVideoDecoder=OMXBaseVideoDecoder \
        -DGstBaseVideoDecoderClass=OMXBaseVideoDecoderClass

libgstomx_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API=1 \
	-I$(srcdir)/openmax \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(fixbaseclasses)
libgstomx_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstomx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = openmax gstomx.conf

