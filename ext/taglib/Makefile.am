plugin_LTLIBRARIES = libgsttaglib.la

libgsttaglib_la_SOURCES = gsttaglib.cc
libgsttaglib_la_CXXFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(TAGLIB_CFLAGS)
libgsttaglib_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ \
	$(TAGLIB_LIBS)
libgsttaglib_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsttaglib.h 

#EXTRA_DIST = README
