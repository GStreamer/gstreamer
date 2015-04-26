plugin_LTLIBRARIES = libgstapetag.la

libgstapetag_la_SOURCES = gstapedemux.c
libgstapetag_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstapetag_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@\
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstapetag_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstapetag_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstapedemux.h
