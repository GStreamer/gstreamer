noinst_LTLIBRARIES = libgstpyramidsegment.la

# sources used to compile this plug-in
libgstpyramidsegment_la_SOURCES = gstpyramidsegment.c

# flags used to compile this pyramidsegment
# add other _CFLAGS and _LIBS as needed
libgstpyramidsegment_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS)
libgstpyramidsegment_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS)
libgstpyramidsegment_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstpyramidsegment.h
