# plugindir is set in configure

noinst_LTLIBRARIES = libgstfacedetect.la

# sources used to compile this plug-in
libgstfacedetect_la_SOURCES = gstfacedetect.c

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
libgstfacedetect_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgstfacedetect_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgstfacedetect_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfacedetect.h
