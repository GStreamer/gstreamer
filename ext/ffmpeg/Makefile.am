plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c 		\
			  gstffmpegcodecmap.c	\
			  gstffmpegdec.c 	\
			  gstffmpegdemux.c 	\
			  gstffmpegenc.c 	\
			  gstffmpegmux.c 	\
			  gstffmpegprotocol.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) \
	-I $(top_builddir)/gst-libs/ext/ffmpeg/ffmpeg/libavcodec \
	-I $(top_builddir)/gst-libs/ext/ffmpeg/ffmpeg/libavformat
libgstffmpeg_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/ffmpeg/ffmpeg/libavcodec/libavcodec.a \
	$(top_builddir)/gst-libs/ext/ffmpeg/ffmpeg/libavformat/libavformat.a

libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstffmpegcodecmap.h
