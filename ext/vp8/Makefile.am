plugin_LTLIBRARIES = \
	libgstvp8.la

libgstvp8_la_SOURCES = \
	gstvp8dec.c \
	gstvp8enc.c \
	gstvp8utils.c \
	plugin.c

libgstvp8_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstvp8_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstbasevideo-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(VPX_LIBS)
libgstvp8_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvp8_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvp8dec.h \
	gstvp8enc.h \
	gstvp8utils.h
