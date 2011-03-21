plugin_LTLIBRARIES = libgstjpegformat.la

libgstjpegformat_la_SOURCES = gstjpegformat.c gstjpegparse.c gstjifmux.c
libgstjpegformat_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstjpegformat_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-@GST_MAJORMINOR@ \
    -lgsttag-@GST_MAJORMINOR@ $(GST_LIBS) $(GST_BASE_LIBS)
libgstjpegformat_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstjpegformat_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstjpegformat.h gstjpegparse.h gstjifmux.h
