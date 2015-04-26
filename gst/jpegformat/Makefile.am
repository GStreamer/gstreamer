plugin_LTLIBRARIES = libgstjpegformat.la

libgstjpegformat_la_SOURCES = gstjpegformat.c gstjpegparse.c gstjifmux.c
libgstjpegformat_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpegformat_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstjpegformat_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjpegformat_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstjpegformat.h gstjpegparse.h gstjifmux.h
