plugin_LTLIBRARIES = libgstadpcmenc.la

# sources used to compile this plug-in
libgstadpcmenc_la_SOURCES = adpcmenc.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstadpcmenc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstadpcmenc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_LIBS)
libgstadpcmenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstadpcmenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
