# plugindir is set in configure

plugin_LTLIBRARIES = libgstaudiofxgood.la

# sources used to compile this plug-in
libgstaudiofxgood_la_SOURCES = audiofx.c\
	audiopanorama.c

# flags used to compile this plugin
libgstaudiofxgood_la_CFLAGS = $(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS)
libgstaudiofxgood_la_LIBADD = $(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS)
libgstaudiofxgood_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = audiopanorama.h
