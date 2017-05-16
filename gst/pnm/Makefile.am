plugin_LTLIBRARIES = libgstpnm.la

libgstpnm_la_SOURCES = gstpnmutils.c gstpnm.c gstpnmdec.c gstpnmenc.c
libgstpnm_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstpnm_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_API_VERSION@
libgstpnm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpnmdec.h gstpnmutils.h gstpnmenc.h
