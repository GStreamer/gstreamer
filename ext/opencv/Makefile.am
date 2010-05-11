SUBDIRS = basicfilters edgedetect faceblur facedetect pyramidsegment templatematch textwrite

# plugindir is set in configure

plugin_LTLIBRARIES = libgstopencv.la

# sources used to compile this plug-in
libgstopencv_la_SOURCES = gstopencv.c gstopencvbasetrans.c

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
libgstopencv_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) \
								 -I${top_srcdir}/src \
								 -I${top_srcdir}/ext/opencv/basicfilters \
								 -I${top_srcdir}/ext/opencv/edgedetect \
								 -I${top_srcdir}/ext/opencv/faceblur \
								 -I${top_srcdir}/ext/opencv/facedetect \
								 -I${top_srcdir}/ext/opencv/pyramidsegment \
								 -I${top_srcdir}/ext/opencv/templatematch \
								 -I${top_srcdir}/ext/opencv/textwrite 

libgstopencv_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) \
								 $(top_builddir)/ext/opencv/basicfilters/libgstbasicfilters.la \
								 $(top_builddir)/ext/opencv/edgedetect/libgstedgedetect.la \
								 $(top_builddir)/ext/opencv/faceblur/libgstfaceblur.la \
								 $(top_builddir)/ext/opencv/facedetect/libgstfacedetect.la \
								 $(top_builddir)/ext/opencv/pyramidsegment/libgstpyramidsegment.la \
								 $(top_builddir)/ext/opencv/templatematch/libgsttemplatematch.la \
								 $(top_builddir)/ext/opencv/textwrite/libgsttextwrite.la

libgstopencv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstopencv_la_DEPENDENCIES = \
								 $(top_builddir)/ext/opencv/basicfilters/libgstbasicfilters.la \
								 $(top_builddir)/ext/opencv/edgedetect/libgstedgedetect.la \
								 $(top_builddir)/ext/opencv/faceblur/libgstfaceblur.la \
								 $(top_builddir)/ext/opencv/facedetect/libgstfacedetect.la \
								 $(top_builddir)/ext/opencv/pyramidsegment/libgstpyramidsegment.la \
								 $(top_builddir)/ext/opencv/templatematch/libgsttemplatematch.la \
								 $(top_builddir)/ext/opencv/textwrite/libgsttextwrite.la

# headers we need but don't want installed
noinst_HEADERS = gstopencvbasetrans.h
