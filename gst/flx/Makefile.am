plugin_LTLIBRARIES = libgstflxdec.la

libgstflxdec_la_SOURCES = gstflxdec.c flx_color.c
libgstflxdec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) 
libgstflxdec_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)
libgstflxdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstflxdec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = flx_fmt.h flx_color.h gstflxdec.h

