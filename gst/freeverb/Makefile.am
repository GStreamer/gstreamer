plugin_LTLIBRARIES = libgstfreeverb.la

# sources used to compile this plug-in
libgstfreeverb_la_SOURCES = gstfreeverb.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstfreeverb_la_CFLAGS = $(GST_CFLAGS) \
  $(GST_BASE_CFLAGS) \
  $(GST_PLUGINS_BASE_CFLAGS)
libgstfreeverb_la_LIBADD = $(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_API_VERSION) \
	$(LIBM)
libgstfreeverb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfreeverb_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfreeverb.h

presetdir = $(datadir)/gstreamer-$(GST_API_VERSION)/presets
preset_DATA = GstFreeverb.prs

EXTRA_DIST = $(preset_DATA)
