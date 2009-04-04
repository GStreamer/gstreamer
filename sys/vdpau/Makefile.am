plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpaudevice.c \
	gstvdpaudecoder.c \
	gstvdpaumpegdecoder.c \
	mpegutil.c \
	gstvdpauvideoyuv.c \
	gstvdpauvideobuffer.c

libgstvdpau_la_CFLAGS = $(GST_CFLAGS) $(X11_CFLAGS) -Ivdpau
libgstvdpau_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE) $(X11_LIBS) -lgstvideo-$(GST_MAJORMINOR) -lvdpau
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpaudevice.h \
	gstvdpaudecoder.h \
	gstvdpaumpegdecoder.h \
	gstvdpauvideoyuv.h \
	gstvdpauvideobuffer.h \
	mpegutil.h
	

