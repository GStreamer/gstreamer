plugin_LTLIBRARIES = libgstopenmpt.la

libgstopenmpt_la_SOURCES = gstopenmptdec.c plugin.c

libgstopenmpt_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(OPENMPT_CFLAGS)
libgstopenmpt_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenmpt_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstbadaudio-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(OPENMPT_LIBS)
libgstopenmpt_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstopenmptdec.h
