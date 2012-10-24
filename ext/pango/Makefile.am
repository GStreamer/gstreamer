plugin_LTLIBRARIES = libgstpango.la

noinst_HEADERS = \
        gstbasetextoverlay.h \
	gstclockoverlay.h \
	gsttextoverlay.h \
	gsttextrender.h \
	gsttimeoverlay.h

libgstpango_la_SOURCES = \
	gstbasetextoverlay.c \
	gstclockoverlay.c \
	gsttextoverlay.c \
	gsttextrender.c \
	gsttimeoverlay.c

libgstpango_la_CFLAGS = \
        $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(PANGO_CFLAGS)
libgstpango_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(PANGO_LIBS)
libgstpango_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpango_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

