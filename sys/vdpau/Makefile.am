plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpdevice.c \
	gstvdpmpegdec.c \
	mpegutil.c \
	gstvdpvideoyuv.c \
	gstvdpvideobuffer.c \
	gstvdp.c \
	gstvdpyuvvideo.c \
	gstvdputils.c \
	gstvdpvideopostprocess.c \
	gstvdpoutputbuffer.c \
	gstvdpsink.c

libgstvdpau_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)
libgstvdpau_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(X11_LIBS) -lgstvideo-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(VDPAU_LIBS)
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpdevice.h \
	gstvdpmpegdec.h \
	mpegutil.h \
	gstvdpvideoyuv.h \
	gstvdpvideobuffer.h \
	gstvdpyuvvideo.h \
	gstvdputils.h \
	gstvdpvideopostprocess.h \
	gstvdpoutputbuffer.h \
	gstvdpsink.h
	

