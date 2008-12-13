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
			  gstffmpegaudioresample.c
# 	\
# 			  gstffmpegscale.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(FFMPEG_CFLAGS)
libgstffmpeg_la_LIBADD = $(FFMPEG_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(LIBM) $(WIN32_LIBS) -lz -lbz2
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstffmpeg_la_LIBTOOLFLAGS = --tag=disable-static

if HAVE_FFMPEG_UNINSTALLED
libgstffmpeg_la_DEPENDENCIES = $(FFMPEG_LIBS)
endif


noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h \
	gstffmpegenc.h \
	gstffmpegcfg.h
