# plugindir is set in configure

plugin_LTLIBRARIES = libgstedgedetect.la

# sources used to compile this plug-in
libgstedgedetect_la_SOURCES = gstedgedetect.c

# flags used to compile this edgedetect
# add other _CFLAGS and _LIBS as needed
libgstedgedetect_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgstedgedetect_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgstedgedetect_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstedgedetect.h
