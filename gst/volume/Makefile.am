plugin_LTLIBRARIES = libgstvolume.la

libgstvolume_la_SOURCES = gstvolume.c
libgstvolume_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvolume_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_MAJORMINOR).la  \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) \
	$(LIBOIL_LIBS)
libgstvolume_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstvolume.h

