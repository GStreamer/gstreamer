
plugin_LTLIBRARIES = libgstjpeg.la

libgstjpeg_la_SOURCES = gstjpeg.c gstjpegdec.c gstjpegenc.c gstsmokeenc.c gstsmokedec.c smokecodec.c
libgstjpeg_la_CFLAGS = $(GST_CFLAGS)
libgstjpeg_la_LIBADD = $(JPEG_LIBS) 
libgstjpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstjpegdec.h gstjpegenc.h gstsmokeenc.h gstsmokedec.h
