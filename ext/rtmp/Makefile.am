plugin_LTLIBRARIES = libgstrtmp.la

libgstrtmp_la_SOURCES = gstrtmpsrc.c

noinst_HEADERS = gstrtmpsrc.h
libgstrtmp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(RTMP_CFLAGS)
libgstrtmp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(RTMP_LIBS)
libgstrtmp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtmp_la_LIBTOOLFLAGS = --tag=disable-static

