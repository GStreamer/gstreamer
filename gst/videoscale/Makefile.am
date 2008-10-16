plugin_LTLIBRARIES = libgstvideoscale.la

libgstvideoscale_la_SOURCES = \
	gstvideoscale.c \
	vs_image.c \
	vs_scanline.c \
	vs_4tap.c

libgstvideoscale_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstvideoscale_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoscale_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBOIL_LIBS)
libgstvideoscale_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvideoscale.h \
	vs_image.h \
	vs_scanline.h \
	vs_4tap.h
