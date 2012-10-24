plugin_LTLIBRARIES = libgstcdparanoia.la

libgstcdparanoia_la_SOURCES = gstcdparanoiasrc.c
libgstcdparanoia_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(CDPARANOIA_CFLAGS)
libgstcdparanoia_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(CDPARANOIA_LIBS)
libgstcdparanoia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcdparanoia_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstcdparanoiasrc.h
