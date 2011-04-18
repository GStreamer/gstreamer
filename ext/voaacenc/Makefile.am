plugin_LTLIBRARIES = libgstvoaacenc.la

libgstvoaacenc_la_SOURCES = \
	gstvoaac.c \
	gstvoaacenc.c 

libgstvoaacenc_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(VOAACENC_CFLAGS)
libgstvoaacenc_la_LIBADD = -lgstaudio-$(GST_MAJORMINOR) \
													 $(GST_BASE_LIBS) $(GST_LIBS) $(VOAACENC_LIBS)
libgstvoaacenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvoaacenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstvoaacenc.h

presetdir = $(datadir)/gstreamer-$(GST_MAJORMINOR)/presets

EXTRA_DIST = $(preset_DATA)
