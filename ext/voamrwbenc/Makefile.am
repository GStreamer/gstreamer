plugin_LTLIBRARIES = libgstvoamrwbenc.la

libgstvoamrwbenc_la_SOURCES = 	\
	gstvoamrwb.c		\
	gstvoamrwbenc.c

libgstvoamrwbenc_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(VOAMRWBENC_CFLAGS)
libgstvoamrwbenc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(VOAMRWBENC_LIBS)
libgstvoamrwbenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvoamrwbenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS =	\
	gstvoamrwbenc.h

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstVoAmrwbEnc.prs

EXTRA_DIST = $(preset_DATA)

