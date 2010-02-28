# plugindir is set in configure

noinst_LTLIBRARIES = libgsttextwrite.la

# sources used to compile this plug-in
libgsttextwrite_la_SOURCES = gsttextwrite.c

# flags used to compile this textwrite
# add other _CFLAGS and _LIBS as needed
libgsttextwrite_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgsttextwrite_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgsttextwrite_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gsttextwrite.h
