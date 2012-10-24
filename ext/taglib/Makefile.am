plugin_LTLIBRARIES = libgsttaglib.la

libgsttaglib_la_SOURCES = gsttaglibplugin.c gstid3v2mux.cc gstapev2mux.cc
libgsttaglib_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(TAGLIB_CFLAGS)
libgsttaglib_la_CXXFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(TAGLIB_CXXFLAGS)
libgsttaglib_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	$(GST_LIBS) \
	$(TAGLIB_LIBS)
libgsttaglib_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttaglib_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstid3v2mux.h gstapev2mux.h
