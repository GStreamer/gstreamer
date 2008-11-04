plugin_LTLIBRARIES = libgstflacparse.la

libgstflacparse_la_SOURCES = gstflac.c gstbaseparse.c gstflacparse.c gstbitreader.c gstbytereader.c
libgstflacparse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstflacparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstflacparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstflacparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstbitreader.h gstbytereader.h gstbaseparse.h gstflacparse.h
