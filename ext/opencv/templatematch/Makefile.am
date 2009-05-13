# plugindir is set in configure

noinst_LTLIBRARIES = libgsttemplatematch.la

# sources used to compile this plug-in
libgsttemplatematch_la_SOURCES = gsttemplatematch.c

# flags used to compile this templatematch
# add other _CFLAGS and _LIBS as needed
libgsttemplatematch_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgsttemplatematch_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgsttemplatematch_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gsttemplatematch.h
