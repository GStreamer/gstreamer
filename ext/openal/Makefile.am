# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstopenal.la

# sources used to compile this plug-in
libgstopenal_la_SOURCES = gstopenal.c gstopenalsink.c gstopenalsrc.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstopenal_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(OPENAL_CFLAGS) $(GST_PLUGINS_BAD_CFLAGS)
libgstopenal_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(OPENAL_LIBS)
libgstopenal_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenal_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstopenalsink.h gstopenalsrc.h
