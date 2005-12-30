plugin_LTLIBRARIES = libgstvolume.la

libgstvolume_la_SOURCES = gstvolume.c
libgstvolume_la_CFLAGS = $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvolume_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
        $(GST_CONTROLLER_LIBS)

noinst_HEADERS = gstvolume.h

