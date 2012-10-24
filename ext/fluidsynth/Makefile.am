# plugindir is set in configure
plugin_LTLIBRARIES =

if USE_FLUIDSYNTH
plugin_LTLIBRARIES += libgstfluidsynthmidi.la

# sources used to compile this plug-in
libgstfluidsynthmidi_la_SOURCES = gstfluiddec.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstfluidsynthmidi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
			     $(GST_BASE_CFLAGS) $(FLUIDSYNTH_CFLAGS)
libgstfluidsynthmidi_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(FLUIDSYNTH_LIBS)
libgstfluidsynthmidi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfluidsynthmidi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
endif

noinst_HEADERS = gstfluiddec.h

