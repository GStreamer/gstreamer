plugin_LTLIBRARIES = libgstladspa.la

libgstladspa_la_SOURCES = gstladspa.c
libgstladspa_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(LRDF_CFLAGS)
libgstladspa_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/signalprocessor/libgstsignalprocessor-@GST_API_VERSION@.la \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(LIBM) $(LRDF_LIBS)
libgstladspa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstladspa_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstladspa.h
