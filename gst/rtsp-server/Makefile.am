public_headers = \
		rtsp-auth.h \
		rtsp-params.h \
		rtsp-sdp.h \
		rtsp-media.h \
		rtsp-media-factory.h \
		rtsp-media-factory-uri.h \
		rtsp-media-mapping.h \
		rtsp-session.h \
		rtsp-session-pool.h \
		rtsp-client.h \
		rtsp-server.h

c_sources = \
	rtsp-funnel.c \
	rtsp-auth.c \
	rtsp-params.c \
	rtsp-sdp.c \
	rtsp-media.c \
	rtsp-media-factory.c \
	rtsp-media-factory-uri.c \
	rtsp-media-mapping.c \
	rtsp-session.c \
	rtsp-session-pool.c \
	rtsp-client.c \
	rtsp-server.c

noinst_HEADERS = rtsp-funnel.h

lib_LTLIBRARIES = \
	libgstrtspserver-@GST_MAJORMINOR@.la

libgstrtspserver_@GST_MAJORMINOR@_la_SOURCES = \
    $(c_sources)

libgstrtspserver_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtspserver_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtspserver_@GST_MAJORMINOR@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
    -lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
            -lgstsdp-@GST_MAJORMINOR@ \
            -lgstapp-@GST_MAJORMINOR@ \
	    $(GST_LIBS) $(LIBM)
libgstrtspserver_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static

libgstrtspserver_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtsp-server
libgstrtspserver_@GST_MAJORMINOR@include_HEADERS = $(public_headers)

CLEANFILES =

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRtspServer-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstrtspserver_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstrtspserver_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/rtsp-server/%',$(libgstrtspinclude_HEADERS))

GstRtspServer-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstrtspserver-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRtspServer \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		-I$(top_srcdir) \
		-I$(top_builddir) \
		-DIN_GOBJECT_INTROSPECTION=1 \
		--c-include='gst/gst.h' \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-rtsp-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-sdp-@GST_MAJORMINOR@` \
		--library=libgstrtspserver-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--include=GstRtsp-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg gstreamer-rtsp-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-rtsp-server-@GST_MAJORMINOR@ \
		--output $@ \
		$(gir_headers) \
		$(gir_sources)

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/

typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
	$(INTROSPECTION_COMPILER) \
	--includedir=$(srcdir) \
	--includedir=$(builddir) \
	--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
	--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-rtsp-@GST_MAJORMINOR@` \
	--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-sdp-@GST_MAJORMINOR@` \
	$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
