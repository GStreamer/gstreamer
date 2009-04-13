plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpdevice.c \
	gstvdpdecoder.c \
	gstvdpmpegdecoder.c \
	mpegutil.c \
	gstvdpvideoyuv.c \
	gstvdpvideobuffer.c \
	gstvdp.c \
	gstvdpyuvvideo.c

libgstvdpau_la_CFLAGS = $(GST_CFLAGS) $(X11_CFLAGS) -Ivdpau
libgstvdpau_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE) $(X11_LIBS) -lgstvideo-$(GST_MAJORMINOR) -lvdpau
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpdevice.h \
	gstvdpdecoder.h \
	gstvdpmpegdecoder.h \
	mpegutil.h \
	gstvdpvideoyuv.h \
	gstvdpvideobuffer.h \
	gstvdpyuvvideo.h
	

