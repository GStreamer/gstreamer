plugin_LTLIBRARIES = libgstsid.la

libgstsid_la_SOURCES = gstsiddec.cc
libgstsid_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CXXFLAGS) $(SIDPLAY_CFLAGS)
libgstsid_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(SIDPLAY_LIBS) \
		      -lgstaudio-$(GST_API_VERSION)
libgstsid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsid_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsiddec.h
