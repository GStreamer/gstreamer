plugin_LTLIBRARIES = libgstladspa.la

libgstladspa_la_SOURCES = \
	gstladspautils.c \
	gstladspafilter.c \
	gstladspasource.c \
	gstladspasink.c \
	gstladspa.c
libgstladspa_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(LRDF_CFLAGS) \
	$(GST_PLUGINS_BAD_CFLAGS)
libgstladspa_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM) \
	$(LRDF_LIBS) \
	$(GST_LIBS) \
	$(GMODULE_NO_EXPORT_LIBS)
libgstladspa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstladspautils.h \
	gstladspafilter.h \
	gstladspasource.h \
	gstladspasink.h \
	gstladspa.h
