public_headers = \
		rtsp-server.h \
		rtsp-client.h \
		rtsp-media.h \
		rtsp-media-factory.h \
		rtsp-media-mapping.h \
		rtsp-params.h \
		rtsp-sdp.h \
		rtsp-session-pool.h \
		rtsp-session.h
		
c_sources = \
	rtsp-server.c \
	rtsp-client.c \
	rtsp-media.c \
	rtsp-media-factory.c \
        rtsp-media-mapping.c \
        rtsp-params.c \
        rtsp-sdp.c \
	rtsp-session-pool.c \
	rtsp-session.c
	
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
