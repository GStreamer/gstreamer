
noinst_PROGRAMS = gst-rtsp-server

INCLUDES = -I$(top_srcdir) -I$(srcdir)

gst_rtsp_server_SOURCES = \
	main.c 

gst_rtsp_server_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
gst_rtsp_server_LDFLAGS = \
	$(GST_LIBS) \
	$(top_builddir)/gst/rtsp-server/libgstrtspserver.la

