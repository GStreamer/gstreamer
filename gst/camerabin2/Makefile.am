plugin_LTLIBRARIES = libgstcamerabin2.la

libgstcamerabin2_la_SOURCES = gstviewfinderbin.c

libgstcamerabin2_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)

libgstcamerabin2_la_LIBADD = \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)

libgstcamerabin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstviewfinderbin.h
