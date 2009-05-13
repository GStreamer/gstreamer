SUBDIRS = edgedetect facedetect pyramidsegment templatematch

# plugindir is set in configure

plugin_LTLIBRARIES = libgstopencv.la

# sources used to compile this plug-in
libgstopencv_la_SOURCES = gstopencv.c

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
libgstopencv_la_CFLAGS = $(GST_CFLAGS) $(OPENCV_CFLAGS) \
								 -I${top_srcdir}/ext/opencv/edgedetect \
								 -I${top_srcdir}/ext/opencv/facedetect \
								 -I${top_srcdir}/ext/opencv/pyramidsegment \
								 -I${top_srcdir}/ext/opencv/templatematch

libgstopencv_la_LIBADD = $(GST_LIBS) $(OPENCV_LIBS) \
								 $(top_builddir)/ext/opencv/edgedetect/libgstedgedetect.la \
								 $(top_builddir)/ext/opencv/facedetect/libgstfacedetect.la \
								 $(top_builddir)/ext/opencv/pyramidsegment/libgstpyramidsegment.la \
								 $(top_builddir)/ext/opencv/templatematch/libgsttemplatematch.la

libgstopencv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstopencv_la_DEPENDENCIES = \
								 $(top_builddir)/ext/opencv/edgedetect/libgstedgedetect.la \
								 $(top_builddir)/ext/opencv/facedetect/libgstfacedetect.la \
								 $(top_builddir)/ext/opencv/pyramidsegment/libgstpyramidsegment.la \
								 $(top_builddir)/ext/opencv/templatematch/libgsttemplatematch.la

# headers we need but don't want installed
noinst_HEADERS = 
