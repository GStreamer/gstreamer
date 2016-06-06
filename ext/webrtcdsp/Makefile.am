plugin_LTLIBRARIES = libgstwebrtcdsp.la

libgstwebrtcdsp_la_SOURCES = gstwebrtcdsp.cpp gstwebrtcechoprobe.cpp
libgstwebrtcdsp_la_CXXFLAGS = \
	-std=c++11 \
	$(GST_CXXFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(WEBRTCDSP_CFLAGS)
libgstwebrtcdsp_la_LIBADD = \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_LIBS) $(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(WEBRTCDSP_LIBS)
libgstwebrtcdsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstwebrtcdsp.h gstwebrtcechoprobe.h
