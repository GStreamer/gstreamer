
plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c 		\
			  gstffmpegcodecmap.c	\
			  gstffmpegdec.c 	\
			  gstffmpegdemux.c 	\
			  gstffmpegenc.c 	\
			  gstffmpegmux.c 	\
			  gstffmpegprotocol.c	\
			  gstffmpegcolorspace.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/ffmpeg/libavcodec \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/ffmpeg/libavformat
libgstffmpeg_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavcodec.la \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavformat.la

libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstffmpegcodecmap.h
