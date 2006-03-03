plugin_LTLIBRARIES = libgstjpeg.la

libgstjpeg_la_SOURCES = \
	gstjpeg.c \
	gstjpegenc.c \
	gstjpegdec.c \
	gstsmokeenc.c \
	smokecodec.c \
	gstsmokedec.c

libgstjpeg_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpeg_la_LIBADD = $(GST_LIBS) $(JPEG_LIBS) 
libgstjpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstjpegdec.h gstjpegenc.h \
	gstsmokeenc.h gstsmokedec.h \
	smokecodec.h smokeformat.h
