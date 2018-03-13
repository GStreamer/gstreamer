lib_LTLIBRARIES = libgstopencv-@GST_API_VERSION@.la

libgstopencv_@GST_API_VERSION@_la_SOURCES = \
	gstopencvutils.cpp \
	gstopencvvideofilter.cpp

libgstopencv_@GST_API_VERSION@_la_CXXFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) \
	$(OPENCV_CFLAGS)

libgstopencv_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
	$(OPENCV_LIBS) -lgstvideo-$(GST_API_VERSION)

libgstopencv_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstopencv_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/opencv
libgstopencv_@GST_API_VERSION@include_HEADERS = \
	opencv-prelude.h \
	gstopencvutils.h \
	gstopencvvideofilter.h
