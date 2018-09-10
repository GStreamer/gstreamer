plugin_LTLIBRARIES = libgstwebrtc.la

noinst_HEADERS = \
	fwd.h \
	gstwebrtcbin.h \
	gstwebrtcice.h \
	gstwebrtcstats.h \
	icestream.h \
	nicetransport.h \
	sctptransport.h \
	transportstream.h \
	transportsendbin.h \
	transportreceivebin.h \
	utils.h \
	webrtcsdp.h \
	webrtcdatachannel.h \
	webrtctransceiver.h

libgstwebrtc_la_SOURCES = \
	gstwebrtc.c \
	gstwebrtcbin.c \
	gstwebrtcice.c \
	gstwebrtcstats.c \
	icestream.c \
	nicetransport.c \
	sctptransport.c \
	transportstream.c \
	transportsendbin.c \
	transportreceivebin.c \
	utils.c \
	webrtcsdp.c \
	webrtcdatachannel.c \
	webrtctransceiver.c

libgstwebrtc_la_SOURCES += $(BUILT_SOURCES)
noinst_HEADERS += $(built_headers)

libgstwebrtc_la_CFLAGS = \
	-I$(top_builddir)/gst-libs \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS) \
	$(NICE_CFLAGS)
libgstwebrtc_la_LIBADD = \
	-lgstapp-@GST_API_VERSION@ \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS) \
	$(NICE_LIBS) \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/sctp/libgstsctp-@GST_API_VERSION@.la

libgstwebrtc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwebrtc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

include $(top_srcdir)/common/gst-glib-gen.mak
