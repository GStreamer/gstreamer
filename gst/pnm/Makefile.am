plugin_LTLIBRARIES = libgstpnm.la

libgstpnm_la_SOURCES = gstpnmutils.c gstpnm.c gstpnmdec.c gstpnmenc.c
libgstpnm_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstpnm_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_MAJORMINOR@
libgstpnm_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpnmdec.h gstpnmutils.h gstpnmenc.h
