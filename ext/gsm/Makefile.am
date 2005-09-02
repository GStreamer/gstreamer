plugin_LTLIBRARIES = libgstgsm.la

libgstgsm_la_SOURCES = gstgsm.c gstgsmenc.c
libgstgsm_la_CFLAGS = $(GST_CFLAGS) $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstgsm_la_LIBADD = $(GSM_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@
libgstgsm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstgsmenc.h
