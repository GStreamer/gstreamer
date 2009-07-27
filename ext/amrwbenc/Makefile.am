plugin_LTLIBRARIES = libgstamrwbenc.la

libgstamrwbenc_la_SOURCES = 	\
	gstamrwb.c		\
	gstamrwbenc.c

libgstamrwbenc_la_CFLAGS = $(GST_CFLAGS) $(AMRWB_CFLAGS)
libgstamrwbenc_la_LIBADD = $(GST_BASE_LIBS) $(AMRWB_LIBS)
libgstamrwbenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamrwbenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS =	\
	gstamrwbenc.h

presetdir = $(datadir)/gstreamer-$(GST_MAJORMINOR)/presets
preset_DATA = GstAmrwbEnc.prs

EXTRA_DIST = $(preset_DATA)

