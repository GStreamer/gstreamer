plugin_LTLIBRARIES = libgstrtpmux.la

libgstrtpmux_la_SOURCES = gstrtpmux.c

libgstrtpmux_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS) -DEXTERN_BUF -DRTP_SUPPORT
libgstrtpmux_la_LIBADD = $(GST_LIBS_LIBS)
libgstrtpmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@
