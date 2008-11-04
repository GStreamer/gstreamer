plugin_LTLIBRARIES = libgsteffectv.la

libgsteffectv_la_SOURCES = \
	gsteffectv.c gstedge.c gstaging.c gstdice.c gstwarp.c \
	gstshagadelic.c gstvertigo.c gstrev.c gstquark.c
libgsteffectv_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst/videofilter
libgsteffectv_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
libgsteffectv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsteffectv_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsteffectv.h
