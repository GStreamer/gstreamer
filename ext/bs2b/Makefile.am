plugin_LTLIBRARIES = libgstbs2b.la

libgstbs2b_la_SOURCES = gstbs2b.c gstbs2b.h

libgstbs2b_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(BS2B_CFLAGS)
libgstbs2b_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(BS2B_LIBS)
libgstbs2b_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstbs2b_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstbs2b.h
