plugin_LTLIBRARIES = libgstlibav.la

if HAVE_BZ2
    BZ2_LIBS = -lbz2
else
    BZ2_LIBS =
endif

libgstlibav_la_SOURCES = gstffmpeg.c	\
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

libgstlibav_la_CFLAGS = $(LIBAV_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstlibav_la_LIBADD = $(LIBAV_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) \
	$(LIBM) $(WIN32_LIBS) -lz $(BZ2_LIBS)
libgstlibav_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DARWIN_LDFLAGS)
libgstlibav_la_LIBTOOLFLAGS = --tag=disable-static

if HAVE_LIBAV_UNINSTALLED
libgstlibav_la_DEPENDENCIES = $(LIBAV_LIBS)
endif


noinst_HEADERS = \
	gstffmpeg.h \
	gstffmpegcodecmap.h \
	gstffmpegutils.h \
	gstffmpegenc.h \
	gstffmpegcfg.h \
	gstffmpegpipe.h
