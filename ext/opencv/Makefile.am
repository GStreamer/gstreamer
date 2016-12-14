plugin_LTLIBRARIES = libgstopencv.la

# sources used to compile this plug-in
libgstopencv_la_SOURCES = gstopencv.cpp \
			gstcvdilate.cpp \
			gstcvdilateerode.cpp \
			gstcvequalizehist.cpp \
			gstcverode.cpp \
			gstcvlaplace.cpp \
			gstcvsmooth.cpp \
			gstcvsobel.cpp \
			gstedgedetect.cpp \
			gstfaceblur.cpp \
			gsthanddetect.cpp \
			gsttemplatematch.cpp \
			gsttextoverlay.cpp \
			gstmotioncells.cpp \
			gstskindetect.cpp \
			gstretinex.cpp \
			gstfacedetect.cpp \
			gstsegmentation.cpp \
			gstgrabcut.cpp \
			gstdisparity.cpp \
			motioncells_wrapper.cpp \
			MotionCells.cpp \
			gstdewarp.cpp

libgstopencv_la_CXXFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(OPENCV_CFLAGS) \
	-DGST_HAAR_CASCADES_DIR=\"$(pkgdatadir)/@GST_API_VERSION@/opencv_haarcascades\"

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
#
# override CV_INLINE: GLib will take care of defining 'inline' sufficiently and
# OpenCV's define isn't good enough to avoid 'unused' gcc warnings (at v2.1.0)
libgstopencv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(OPENCV_CFLAGS) \
	-DCV_INLINE="static inline" \
	-DCV_NO_BACKWARD_COMPATIBILITY

libgstopencv_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/opencv/libgstopencv-$(GST_API_VERSION).la \
	$(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) \
	$(GSTPB_BASE_LIBS) -lgstvideo-$(GST_API_VERSION)

libgstopencv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopencv_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
		gstcvdilateerode.h \
		gstcvdilate.h \
		gstcvequalizehist.h \
		gstcverode.h \
		gstcvlaplace.h \
		gstcvsmooth.h \
		gstcvsobel.h \
		gstedgedetect.h \
		gstfaceblur.h \
		gstfacedetect.h \
		gsthanddetect.h \
		gsttemplatematch.h \
		gsttextoverlay.h \
		gstskindetect.h \
		gstretinex.h \
		gstsegmentation.h \
		gstgrabcut.h \
		gstdisparity.h \
		gstmotioncells.h \
		motioncells_wrapper.h \
		MotionCells.h \
		gstdewarp.h

opencv_haarcascadesdir = $(pkgdatadir)/$(GST_API_VERSION)/opencv_haarcascades
opencv_haarcascades_DATA = fist.xml palm.xml

EXTRA_DIST = $(opencv_haarcascades_DATA)
