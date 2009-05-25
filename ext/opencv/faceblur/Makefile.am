# plugindir is set in configure

noinst_LTLIBRARIES = libgstfaceblur.la

# sources used to compile this plug-in
libgstfaceblur_la_SOURCES = gstfaceblur.c

# flags used to compile this faceblur
# add other _CFLAGS and _LIBS as needed
libgstfaceblur_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgstfaceblur_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgstfaceblur_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstfaceblur.h
