SUBDIRS = basevideodecoder gstvdp

plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpau.c \
	gstvdpvideopostprocess.c \
	gstvdpsink.c \
	mpeg/gstvdpmpegframe.c \
	mpeg/mpegutil.c \
	mpeg/gstvdpmpegdec.c \
	h264/gstnalreader.c \
	h264/gsth264parser.c \
	h264/gsth264frame.c \
	h264/gsth264dpb.c \
	h264/gstvdph264dec.c \
	mpeg4/mpeg4util.c \
	mpeg4/gstmpeg4frame.c \
	mpeg4/gstvdpmpeg4dec.c

libgstvdpau_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)

libgstvdpau_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(X11_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	-lgstinterfaces-$(GST_MAJORMINOR) $(VDPAU_LIBS) \
	gstvdp/libgstvdp-@GST_MAJORMINOR@.la \
	$(LIBM)
	
libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvdpau_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvdpvideopostprocess.h \
	gstvdpsink.h \
	mpeg/gstvdpmpegframe.h \
	mpeg/mpegutil.h \
	mpeg/gstvdpmpegdec.h \
	h264/gstnalreader.h \
	h264/gsth264parser.h \
	h264/gsth264frame.h \
	h264/gsth264dpb.h \
	h264/gstvdph264dec.h \
	mpeg4/mpeg4util.h \
	mpeg4/gstmpeg4frame.h \
	mpeg4/gstvdpmpeg4dec.h
