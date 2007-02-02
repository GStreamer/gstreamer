plugin_LTLIBRARIES = libgstequalizer.la

libgstequalizer_la_SOURCES = gstiirequalizer.c
libgstequalizer_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstequalizer_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR)
libgstequalizer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
