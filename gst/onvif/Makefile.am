plugin_LTLIBRARIES = libgstrtponvif.la

libgstrtponvif_la_SOURCES = \
	gstrtponvif.c \
	gstrtponviftimestamp.c gstrtponviftimestamp.h \
	gstrtponvifparse.c gstrtponvifparse.h
libgstrtponvif_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtponvif_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) -lgstrtp-$(GST_API_VERSION)
libgstrtponvif_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtponvif_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
