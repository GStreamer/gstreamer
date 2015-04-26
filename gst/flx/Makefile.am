plugin_LTLIBRARIES = libgstflxdec.la

libgstflxdec_la_SOURCES = gstflxdec.c flx_color.c
libgstflxdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstflxdec_la_LIBADD = $(GST_BASE_LIBS)  $(GST_LIBS)
libgstflxdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstflxdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = flx_fmt.h flx_color.h gstflxdec.h
