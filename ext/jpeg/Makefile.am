plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstjpeg.la

libgstjpeg_la_SOURCES = gstjpeg.c gstjpegdec.c gstjpegenc.c
libgstjpeg_la_LIBADD = $(JPEG_LIBS) 
libgstjpeg_la_CFLAGS = $(GST_CFLAGS)
## FIXME: check these CFLAGS
## -O3 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math -DNDEBUG

noinst_HEADERS = gstjpegdec.h gstjpegenc.h
