# plugindir is set in configure
plugin_LTLIBRARIES =

if USE_FLUIDSYNTH
plugin_LTLIBRARIES += libgstfluidsynth.la

# sources used to compile this plug-in
libgstfluidsynth_la_SOURCES = gstfluidsynth.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstfluidsynth_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
			     $(GST_BASE_CFLAGS) $(FLUIDSYNTH_CFLAGS)
libgstfluidsynth_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(FLUIDSYNTH_LIBS)
libgstfluidsynth_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfluidsynth_la_LIBTOOLFLAGS = --tag=disable-static
endif

noinst_HEADERS = gstfluidsynth.h

