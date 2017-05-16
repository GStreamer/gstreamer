plugin_LTLIBRARIES = libgstrtspclientsink.la

libgstrtspclientsink_la_SOURCES = gstrtspclientsink.c plugin.c

libgstrtspclientsink_la_CFLAGS = -I$(top_srcdir) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)

# FIXME: Hack to avoid having to add GETTEXT_PACKAGE to gst-rtsp
libgstrtspclientsink_la_CFLAGS += -D"GETTEXT_PACKAGE=gst-rtsp-server-1.0"

libgstrtspclientsink_la_LIBADD = $(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_API_VERSION@.la  \
			$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       -lgstrtp-@GST_API_VERSION@ -lgstrtsp-@GST_API_VERSION@ \
		       -lgstsdp-@GST_API_VERSION@ $(GST_NET_LIBS) $(GST_LIBS) \
			$(GIO_LIBS)
libgstrtspclientsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstrtspclientsink.h
