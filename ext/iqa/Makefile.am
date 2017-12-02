plugin_LTLIBRARIES = libgstiqa.la

libgstiqa_la_SOURCES = \
	iqa.c

libgstiqa_la_CFLAGS =  \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstiqa_la_CFLAGS += $(DSSIM_CFLAGS)

libgstiqa_la_LIBADD =  \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstiqa_la_LIBADD += $(DSSIM_LIBS)

libgstiqa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	iqa.h

