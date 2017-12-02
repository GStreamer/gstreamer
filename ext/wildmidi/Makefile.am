# plugindir is set in configure
plugin_LTLIBRARIES =

if USE_WILDMIDI
plugin_LTLIBRARIES += libgstwildmidi.la

# sources used to compile this plug-in
libgstwildmidi_la_SOURCES = gstwildmididec.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstwildmidi_la_CFLAGS = \
        -I$(top_srcdir)/gst-libs \
        -I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
        $(GST_CFLAGS) $(WILDMIDI_CFLAGS)
libgstwildmidi_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstbadaudio-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
        $(GST_LIBS) $(GST_BASE_LIBS) $(WILDMIDI_LIBS)
libgstwildmidi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
endif

noinst_HEADERS = gstwildmididec.h

