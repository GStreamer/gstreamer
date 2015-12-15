plugin_LTLIBRARIES = \
	libgstvpx.la

libgstvpx_la_SOURCES = \
    gstvpxdec.c \
    gstvpxenc.c \
	gstvp8dec.c \
	gstvp8enc.c \
	gstvp8utils.c \
	gstvp9dec.c \
	gstvp9enc.c \
	plugin.c

libgstvpx_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(VPX_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstvpx_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(VPX_LIBS)
libgstvpx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvpx_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
    gstvpxdec.h \
    gstvpxenc.h \
	gstvp8dec.h \
	gstvp8enc.h \
	gstvp9dec.h \
	gstvp9enc.h \
	gstvp8utils.h

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstVP8Enc.prs

EXTRA_DIST = $(preset_DATA)
