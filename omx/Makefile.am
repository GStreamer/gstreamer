plugin_LTLIBRARIES = libgstomx.la

libgstomx_la_SOURCES = \
	gstomx.c \
	gstomxvideodec.c \
	gstomxmpeg4videodec.c \
	gstbasevideocodec.c \
	gstbasevideodecoder.c \
	gstbasevideoencoder.c \
	gstbasevideoutils.c

noinst_HEADERS = \
	gstomxvideodec.h \
	gstomxmpeg4videodec.h \
	gstbasevideocodec.h \
	gstbasevideodecoder.h \
	gstbasevideoencoder.h

libgstomx_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API=1 \
	-I$(srcdir)/openmax \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstomx_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstomx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = openmax

