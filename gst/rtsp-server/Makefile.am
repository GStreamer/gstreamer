public_headers = \
		rtsp-params.h \
		rtsp-sdp.h \
		rtsp-media.h \
		rtsp-media-factory.h \
		rtsp-media-mapping.h \
		rtsp-session.h \
		rtsp-session-pool.h \
		rtsp-client.h \
		rtsp-server.h
		
c_sources = \
	rtsp-params.c \
	rtsp-sdp.c \
	rtsp-media.c \
	rtsp-media-factory.c \
	rtsp-media-mapping.c \
	rtsp-session.c \
	rtsp-session-pool.c \
	rtsp-client.c \
	rtsp-server.c
	
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

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --strip-prefix=Gst
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION
introspection_sources = $(public_headers) $(c_sources)

GstRtspServer-0.10.gir: $(INTROSPECTION_SCANNER) libgstrtspserver-@GST_MAJORMINOR@.la
GstRtspServer_0_10_gir_INCLUDES = Gst-@GST_MAJORMINOR@ GstRtsp-@GST_MAJORMINOR@
GstRtspServer_0_10_gir_CFLAGS = $(INCLUDES)
GstRtspServer_0_10_gir_LIBS = libgstrtspserver-@GST_MAJORMINOR@.la
GstRtspServer_0_10_gir_FILES = $(introspection_sources)
GstRtspServer_0_10_gir_NAMESPACE = GstRtspServer
GstRtspServer_0_10_gir_VERSION = @GST_MAJORMINOR@
INTROSPECTION_GIRS += GstRtspServer-0.10.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif
