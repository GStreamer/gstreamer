plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c gstffmpegenc.c gstffmpegdec.c

## FIXME: these really need a kick in the pants !
## CFLAGS += -I/opt/src/ffmpeg/
## LDFLAGS += -L/opt/src/ffmpeg/libavcodec -lavcodec -L/opt/src/ffmpeg/libav -lav

noinst_HEADERS = gstffmpegenc.h gstffmpegdec.h
