# plugindir is set in configure

# change libgstplugin.la to something more suitable
plugin_LTLIBRARIES = libgstannodex.la

# for the next set of variables, rename the prefix if you renamed the .la

# sources used to compile this plug-in
libgstannodex_la_SOURCES = \
	gstannodex.c \
	gstcmmlutils.c \
	gstcmmldec.c \
	gstcmmlenc.c \
	gstcmmltag.c \
	gstcmmlparser.c

# flags used to compile this plugin
# we use the GST_LIBS flags because we might be using plug-in libs
libgstannodex_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
			  $(GST_CFLAGS) $(XML_CFLAGS)
libgstannodex_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
			  $(GST_LIBS) $(XML_LIBS)
libgstannodex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstannodex.h gstcmmlutils.h gstcmmltag.h gstcmmlparser.h \
	gstcmmldec.h gstcmmlenc.h
