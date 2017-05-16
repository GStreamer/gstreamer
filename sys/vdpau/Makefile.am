plugin_LTLIBRARIES = libgstvdpau.la

libgstvdpau_la_SOURCES = \
	gstvdpau.c \
	gstvdputils.c \
	gstvdpvideomemory.c \
	gstvdpvideobufferpool.c \
	gstvdpdevice.c \
	gstvdpdecoder.c \
	mpeg/gstvdpmpegdec.c
 # \
 # 	h264/gsth264dpb.c \
 # 	h264/gstvdph264dec.c


libgstvdpau_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)

libgstvdpau_la_LIBADD = \
        $(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
	$(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) $(X11_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(VDPAU_LIBS) $(LIBM)

libgstvdpau_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstvdputils.h \
	gstvdpvideomemory.h \
	gstvdpvideobufferpool.h \
	gstvdpdevice.h \
	gstvdpdecoder.h \
	gstvdpoutputbuffer.h \
	gstvdpvideopostprocess.h \
	gstvdpsink.h \
	mpeg/gstvdpmpegdec.h \
	mpeg4/mpeg4util.h \
	mpeg4/gstmpeg4frame.h \
	mpeg4/gstvdpmpeg4dec.h

 # h264/gsth264dpb.h \
 # h264/gstvdph264dec.h

