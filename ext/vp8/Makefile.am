plugin_LTLIBRARIES = \
	libgstvp8.la

libgstvp8_la_SOURCES = \
	gstvp8dec.c \
	gstvp8enc.c \
	plugin.c \
	gst/video/gstbasevideocodec.c \
	gst/video/gstbasevideodecoder.c \
	gst/video/gstbasevideoencoder.c \
	gst/video/gstbasevideoparse.c \
	gst/video/gstbasevideoutils.c \
	gst/video/gstvideocompat.c

fixbaseclasses = \
        -DGstBaseVideoCodec=VP8BaseVideoCodec \
        -DGstBaseVideoCodecClass=VP8BaseVideoCodecClass \
        -DGstBaseVideoEncoder=VP8BaseVideoEncoder \
        -DGstBaseVideoEncoderClass=VP8BaseVideoEncoderClass \
        -DGstBaseVideoDecoder=VP8BaseVideoDecoder \
        -DGstBaseVideoDecoderClass=VP8BaseVideoDecoderClass \
        -DGstBaseVideoParse=VP8BaseVideoParse \
        -DGstBaseVideoParseClass=VP8BaseVideoParseClass

libgstvp8_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(fixbaseclasses)
libgstvp8_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvp8_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstvp8_la_LIBADD += $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ -lgstvideo-@GST_MAJORMINOR@
libgstvp8_la_LIBADD += $(VPX_LIBS)

noinst_HEADERS = \
	gst/video/gstbasevideocodec.h \
	gst/video/gstbasevideodecoder.h \
	gst/video/gstbasevideoencoder.h \
	gst/video/gstbasevideoparse.h \
	gst/video/gstbasevideoutils.h \
	gst/video/gstvideocompat.h

