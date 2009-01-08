public_headers = \
		rtsp-server.h \
		rtsp-client.h \
		rtsp-media.h
		
c_sources = \
	rtsp-server.c \
	rtsp-client.c \
	rtsp-media.c \
	rtsp-session-pool.c \
	rtsp-session.c

lib_LTLIBRARIES = \
	libgstrtspserver.la

libgstrtspserver_la_SOURCES = \
    $(c_sources)
    
libgstrtspserver_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtspserver_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtspserver_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
    -lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
            -lgstsdp-@GST_MAJORMINOR@ $(GST_LIBS) $(LIBM)
libgstrtspserver_la_LIBTOOLFLAGS = --tag=disable-static

libgstrtspserver_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtsp-server
libgstrtspserver_include_HEADERS = $(public_headers)
