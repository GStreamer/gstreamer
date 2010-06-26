plugin_LTLIBRARIES = libgstjpeg.la

libgstjpeg_la_SOURCES = \
	gstjpeg.c \
	gstjpegenc.c \
	gstjpegdec.c \
	gstsmokeenc.c \
	smokecodec.c \
	gstsmokedec.c

libgstjpeg_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpeg_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(JPEG_LIBS) $(LIBM)
libgstjpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjpeg_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstjpeg.h \
	gstjpegdec.h gstjpegenc.h \
	gstsmokeenc.h gstsmokedec.h \
	smokecodec.h smokeformat.h
