plugin_LTLIBRARIES = libgstfaceoverlay.la

# sources used to compile this plug-in
libgstfaceoverlay_la_SOURCES = gstfaceoverlay.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstfaceoverlay_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstfaceoverlay_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_LIBS)
libgstfaceoverlay_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfaceoverlay.h
