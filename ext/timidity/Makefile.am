# plugindir is set in configure

# change libgstplugin.la to something more suitable
plugin_LTLIBRARIES = libgsttimidity.la

# for the next set of variables, rename the prefix if you renamed the .la

# sources used to compile this plug-in
libgsttimidity_la_SOURCES = gsttimidity.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgsttimidity_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(TIMIDITY_CFLAGS)
libgsttimidity_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(TIMIDITY_LIBS)
libgsttimidity_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsttimidity.h
