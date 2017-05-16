plugin_LTLIBRARIES = libgstchromaprint.la

libgstchromaprint_la_SOURCES = gstchromaprint.c gstchromaprint.h

libgstchromaprint_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(CHROMAPRINT_CFLAGS)
libgstchromaprint_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(CHROMAPRINT_LIBS)
libgstchromaprint_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstchromaprint.h
