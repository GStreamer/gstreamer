plugin_LTLIBRARIES = libgstvoamrwbenc.la

libgstvoamrwbenc_la_SOURCES = 	\
	gstvoamrwb.c		\
	gstvoamrwbenc.c

libgstvoamrwbenc_la_CFLAGS = $(GST_CFLAGS) $(VOAMRWBENC_CFLAGS)
libgstvoamrwbenc_la_LIBADD = $(GST_BASE_LIBS) $(VOAMRWBENC_LIBS)
libgstvoamrwbenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvoamrwbenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS =	\
	gstvoamrwbenc.h

presetdir = $(datadir)/gstreamer-$(GST_MAJORMINOR)/presets
preset_DATA = GstVoAmrwbEnc.prs

EXTRA_DIST = $(preset_DATA)

