plugin_LTLIBRARIES = libgsttaglib.la

libgsttaglib_la_SOURCES = gsttaglibmux.c gstid3v2mux.cc gstapev2mux.cc
libgsttaglib_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(TAGLIB_CFLAGS)
libgsttaglib_la_CXXFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CXXFLAGS) \
	$(TAGLIB_CXXFLAGS)
libgsttaglib_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_LIBS) \
	$(TAGLIB_LIBS)
libgsttaglib_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttaglib_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsttaglibmux.h gstid3v2mux.h gstapev2mux.h
