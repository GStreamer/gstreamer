plugin_LTLIBRARIES = libgstapexsink.la

libgstapexsink_la_SOURCES = gstapexplugin.c gstapexraop.c gstapexsink.c
libgstapexsink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(APEXSINK_CFLAGS)
libgstapexsink_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) -lgstinterfaces-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(APEXSINK_LIBS)
libgstapexsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstapexsink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstapexraop.h gstapexsink.h
