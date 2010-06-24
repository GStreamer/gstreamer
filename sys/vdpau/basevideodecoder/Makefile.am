noinst_LTLIBRARIES = libsatbasevideodecoder.la

libsatbasevideodecoder_la_SOURCES = \
	satvideoframe.c \
	satbasevideodecoder.c

libsatbasevideodecoder_la_CFLAGS = $(GST_CFLAGS)
libsatbasevideodecoder_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
libsatbasevideodecoder_la_LDFLAGS = $(GST_ALL_LDFLAGS) -module -avoid-version

noinst_HEADERS = \
	satvideoframe.h \
	satbasevideodecoder.h \
	satbasevideoutils.h