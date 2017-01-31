lib_LTLIBRARIES = libgstwebrtc-@GST_API_VERSION@.la

glib_enum_headers = dtlstransport.h icetransport.h rtptransceiver.h webrtc_fwd.h
glib_enum_define = GST_WEBRTC
glib_gen_prefix = gst_webrtc
glib_gen_basename = webrtc
glib_gen_decl_banner=GST_EXPORT

built_sources = webrtc-enumtypes.c
built_headers = webrtc-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)
CLEANFILES = $(BUILT_SOURCES)

libgstwebrtc_@GST_API_VERSION@_la_SOURCES = \
	dtlstransport.c \
	icetransport.c \
	rtcsessiondescription.c \
	rtpreceiver.c \
	rtpsender.c \
	rtptransceiver.c

nodist_libgstwebrtc_@GST_API_VERSION@_la_SOURCES = $(built_sources)

libgstwebrtc_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/webrtc
libgstwebrtc_@GST_API_VERSION@include_HEADERS = \
	dtlstransport.h \
	icetransport.h \
	rtcsessiondescription.h \
	rtpreceiver.h \
	rtpsender.h \
	rtptransceiver.h \
	webrtc_fwd.h \
	webrtc.h

nodist_libgstwebrtc_@GST_API_VERSION@include_HEADERS = $(built_headers)

libgstwebrtc_@GST_API_VERSION@_la_CFLAGS = \
	-I$(top_builddir)/gst-libs \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GST_SDP_CFLAGS)
libgstwebrtc_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GST_SDP_LIBS)
libgstwebrtc_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

include $(top_srcdir)/common/gst-glib-gen.mak
