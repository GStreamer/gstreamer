noinst_LTLIBRARIES = libgstbasevideodecoder.la

libgstbasevideodecoder_la_SOURCES = \
	gstvideoframe.c \
	gstbasevideodecoder.c

libgstbasevideodecoder_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	-DGstBaseVideoDecoder=SatBaseVideoDecoder \
	-DGstBaseVideoDecoderClass=SatBaseVideoDecoderClass
libgstbasevideodecoder_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstbasevideodecoder_la_LDFLAGS = $(GST_ALL_LDFLAGS) -module -avoid-version

noinst_HEADERS = \
	gstvideoframe.h \
	gstbasevideodecoder.h \
	gstbasevideoutils.h