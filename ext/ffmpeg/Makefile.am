plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c	\
			  gstffmpegprotocol.c	\
			  gstffmpegcodecmap.c	\
			  gstffmpegenc.c	\
			  gstffmpegdec.c	\
			  gstffmpegdemux.c	\
			  gstffmpegdeinterlace.c	\
			  gstffmpegscale.c
#			  gstffmpegmux.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavformat \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavutil \
	-I $(top_srcdir)/gst-libs/ext/ffmpeg/libavcodec
libgstffmpeg_la_LIBADD = \
	$(top_builddir)/gst-libs/ext/ffmpeg/libavformat/libavformat.la \
	$(GST_BASE_LIBS)
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h
