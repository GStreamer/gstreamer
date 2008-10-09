bin_PROGRAMS = gst-rtsp-server

gst_rtsp_server_SOURCES = 	main.c \
			rtsp-server.c \
			rtsp-client.c \
			rtsp-media.c \
			rtsp-session-pool.c \
			rtsp-session.c

gst_rtsp_server_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
gst_rtsp_server_LDADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		    -lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
                    -lgstsdp-@GST_MAJORMINOR@ $(GST_LIBS) $(LIBM)
gst_rtsp_server_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = rtsp-server.h \
		rtsp-client.h \
		rtsp-session.h \
		rtsp-session-pool.h \
		rtsp-media.h
