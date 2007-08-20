plugin_LTLIBRARIES = libgstrtpdtmf.la

libgstrtpdtmf_la_SOURCES = gstrtpdtmfsrc.c

libgstrtpdtmf_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(ERROR_CFLAGS) -DEXTERN_BUF -DRTP_SUPPORT
libgstrtpdtmf_la_LIBADD = $(GST_LIBS_LIBS)
libgstrtpdtmf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@

noinst_HEADERS = gstrtpdtmfsrc.h
