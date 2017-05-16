plugin_LTLIBRARIES = libgstasf.la

libgstasf_la_SOURCES = gstasfdemux.c gstasf.c asfheaders.c asfpacket.c gstrtpasfdepay.c gstrtspwms.c
libgstasf_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstasf_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
                -lgstvideo-@GST_API_VERSION@ \
		-lgstriff-@GST_API_VERSION@ -lgstrtsp-@GST_API_VERSION@ -lgstsdp-@GST_API_VERSION@ \
		-lgstrtp-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ -lgsttag-@GST_API_VERSION@ \
		$(GST_BASE_LIBS) $(GST_LIBS) \
		$(WIN32_LIBS)
libgstasf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstasfdemux.h asfheaders.h asfpacket.h gstrtpasfdepay.h gstrtspwms.h
