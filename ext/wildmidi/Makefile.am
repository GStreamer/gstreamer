# plugindir is set in configure
plugin_LTLIBRARIES =

if USE_WILDMIDI
plugin_LTLIBRARIES += libgstwildmidi.la

# sources used to compile this plug-in
libgstwildmidi_la_SOURCES = gstwildmidi.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstwildmidi_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(WILDMIDI_CFLAGS)
libgstwildmidi_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(WILDMIDI_LIBS)
libgstwildmidi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
endif

noinst_HEADERS = gstwildmidi.h

