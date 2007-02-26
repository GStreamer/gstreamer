lib_LTLIBRARIES = libgstapp-0.10.la

plugin_LTLIBRARIES = libgstapp.la

libgstapp_la_SOURCES = gstapp.c
libgstapp_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstapp_la_LIBADD = $(GST_BASE_LIBS) libgstapp-0.10.la
libgstapp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstapp_0_10_la_SOURCES = gstappsrc.c
libgstapp_0_10_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstapp_0_10_la_LIBADD = $(GST_BASE_LIBS)
libgstapp_0_10_la_LDFLAGS = $(GST_LDFLAGS)

noinst_HEADERS = gstappsrc.h

