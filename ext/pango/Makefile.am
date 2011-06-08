plugin_LTLIBRARIES = libgstpango.la

noinst_HEADERS = \
	gstclockoverlay.h \
	gsttextoverlay.h \
	gsttextrender.h \
	gsttimeoverlay.h

libgstpango_la_SOURCES = \
	gstclockoverlay.c \
	gsttextoverlay.c \
	gsttextrender.c \
	gsttimeoverlay.c

libgstpango_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_CFLAGS) \
	$(PANGO_CFLAGS)
libgstpango_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) \
	$(PANGO_LIBS)
libgstpango_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpango_la_LIBTOOLFLAGS = --tag=disable-static

