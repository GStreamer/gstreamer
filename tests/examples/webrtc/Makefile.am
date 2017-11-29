
noinst_PROGRAMS = webrtc webrtcbidirectional webrtcswap webrtctransceiver

webrtc_SOURCES = webrtc.c
webrtc_CFLAGS=\
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS)
webrtc_LDADD=\
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS) \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la

webrtcbidirectional_SOURCES = webrtcbidirectional.c
webrtcbidirectional_CFLAGS=\
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS)
webrtcbidirectional_LDADD=\
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS) \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la

webrtcswap_SOURCES = webrtcswap.c
webrtcswap_CFLAGS=\
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS)
webrtcswap_LDADD=\
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS) \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la

webrtctransceiver_SOURCES = webrtctransceiver.c
webrtctransceiver_CFLAGS=\
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS)
webrtctransceiver_LDADD=\
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS) \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la
