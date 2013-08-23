plugin_LTLIBRARIES = libgstopencv.la

# sources used to compile this plug-in
libgstopencv_la_SOURCES = gstopencv.c \
			gstopencvvideofilter.c \
			gstopencvutils.c \
			gstcvdilate.c \
			gstcvdilateerode.c \
			gstcvequalizehist.c \
			gstcverode.c \
			gstcvlaplace.c \
			gstcvsmooth.c \
			gstcvsobel.c \
			gstedgedetect.c \
			gstfaceblur.c \
			gstfacedetect.c \
			gsthanddetect.c \
			gstpyramidsegment.c \
			gsttemplatematch.c \
			gsttextoverlay.c \
			gstmotioncells.c \
			gstskindetect.c \
			gstretinex.c \
			gstsegmentation.cpp \
			gstgrabcut.cpp \
			gstdisparity.cpp \
			motioncells_wrapper.cpp \
			MotionCells.cpp

libgstopencv_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(OPENCV_CFLAGS)

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
#
# override CV_INLINE: GLib will take care of defining 'inline' sufficiently and
# OpenCV's define isn't good enough to avoid 'unused' gcc warnings (at v2.1.0)
libgstopencv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(OPENCV_CFLAGS) \
	-DGST_HAAR_CASCADES_DIR=\"$(pkgdatadir)/@GST_API_VERSION@/opencv_haarcascades\" \
	-DCV_INLINE="static inline" \
	-DCV_NO_BACKWARD_COMPATIBILITY

libgstopencv_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) \
			$(GSTPB_BASE_LIBS) -lgstvideo-$(GST_API_VERSION)

libgstopencv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopencv_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstopencvvideofilter.h gstopencvutils.h \
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
		gstpyramidsegment.h \
		gsttemplatematch.h \
		gsttextoverlay.h \
		gstskindetect.h \
		gstretinex.h \
		gstsegmentation.h \
		gstgrabcut.h \
		gstdisparity.h \
		gstmotioncells.h \
		motioncells_wrapper.h \
		MotionCells.h

opencv_haarcascadesdir = $(pkgdatadir)/$(GST_API_VERSION)/opencv_haarcascades
opencv_haarcascades_DATA = fist.xml palm.xml

EXTRA_DIST = $(opencv_haarcascades_DATA)
