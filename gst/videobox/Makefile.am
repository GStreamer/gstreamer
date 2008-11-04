plugin_LTLIBRARIES = libgstvideobox.la

libgstvideobox_la_SOURCES = gstvideobox.c
libgstvideobox_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
			   $(LIBOIL_CFLAGS)
libgstvideobox_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
			   $(LIBOIL_LIBS)
libgstvideobox_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideobox_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = README
