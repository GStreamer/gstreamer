plugin_LTLIBRARIES = libgstrtpmuxer.la

libgstrtpmuxer_la_SOURCES = gstrtpmuxer.c gstrtpmux.c gstrtpdtmfmux.c

libgstrtpmuxer_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS) -DEXTERN_BUF -DRTP_SUPPORT
libgstrtpmuxer_la_LIBADD = $(GST_LIBS_LIBS)
libgstrtpmuxer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@

noinst_HEADERS = gstrtpmux.h gstrtpdtmfmux.h

