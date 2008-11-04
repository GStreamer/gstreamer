plugin_LTLIBRARIES = libgstalsaspdif.la

# sources used to compile this plugin
libgstalsaspdif_la_SOURCES = alsaspdifsink.c

# flags used to compile this plugin
# we use the GST_LIBS flags because we might be using plug-in libs
libgstalsaspdif_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsaspdif_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(GST_LIBS) $(ALSA_LIBS)
libgstalsaspdif_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalsaspdif_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = alsaspdifsink.h

