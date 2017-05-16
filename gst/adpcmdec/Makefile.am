plugin_LTLIBRARIES = libgstadpcmdec.la

# sources used to compile this plug-in
libgstadpcmdec_la_SOURCES = adpcmdec.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstadpcmdec_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstadpcmdec_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstadpcmdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
