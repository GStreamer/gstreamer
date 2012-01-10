plugin_LTLIBRARIES = libgstffmpeg.la

if HAVE_BZ2
    BZ2_LIBS = -lbz2
else
    BZ2_LIBS =
endif

libgstffmpeg_la_SOURCES = gstffmpeg.c	\
			  gstffmpegprotocol.c	\
			  gstffmpegcodecmap.c	\
			  gstffmpegutils.c	\
			  gstffmpegenc.c	\
			  gstffmpegdec.c	\
			  gstffmpegcfg.c	\
			  gstffmpegdemux.c	\
			  gstffmpegmux.c    \
			  gstffmpegdeinterlace.c
#\
#			  gstffmpegaudioresample.c
# 	\
# 			  gstffmpegscale.c

libgstffmpeg_la_CFLAGS = $(FFMPEG_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstffmpeg_la_LIBADD = $(FFMPEG_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstpbutils-$(GST_MAJORMINOR) $(GST_BASE_LIBS) \
	$(LIBM) $(WIN32_LIBS) -lz $(BZ2_LIBS)
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstffmpeg_la_LIBTOOLFLAGS = --tag=disable-static

if HAVE_FFMPEG_UNINSTALLED
libgstffmpeg_la_DEPENDENCIES = $(FFMPEG_LIBS)
endif


noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h \
	gstffmpegutils.h \
	gstffmpegenc.h \
	gstffmpegcfg.h \
	gstffmpegpipe.h
