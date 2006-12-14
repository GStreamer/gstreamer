plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c	\
			  gstffmpegprotocol.c	\
			  gstffmpegcodecmap.c	\
			  gstffmpegenc.c	\
			  gstffmpegdec.c	\
			  gstffmpegcfg.c	\
			  gstffmpegdemux.c	\
			  gstffmpegmux.c    \
			  gstffmpegdeinterlace.c	\
			  gstffmpegscale.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(FFMPEG_CFLAGS)
libgstffmpeg_la_LIBADD = $(FFMPEG_LIBS) $(GST_BASE_LIBS)
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h \
	gstffmpegenc.h \
	gstffmpegcfg.h
