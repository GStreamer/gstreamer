plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c 		\
			  gstffmpegcodecmap.c	\
			  gstffmpegdec.c 	\
			  gstffmpegenc.c	\
			  gstffmpegdeinterlace.c
#			  gstffmpegdemux.c	\
#			  gstffmpegmux.c 	\
#			  gstffmpegprotocol.c	\
#			  gstffmpegscale.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavformat \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavutil \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec
libgstffmpeg_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavformat/libavformat.la \
	$(GST_LIBS)
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h
