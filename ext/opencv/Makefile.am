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
			gstpyramidsegment.c \
			gsttemplatematch.c \
			gsttextwrite.c

# flags used to compile this facedetect
# add other _CFLAGS and _LIBS as needed
libgstopencv_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(OPENCV_CFLAGS) \
			$(GST_PLUGINS_BASE_CFLAGS)

libgstopencv_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(OPENCV_LIBS) \
			$(GSTPB_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR)

libgstopencv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

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
		gstpyramidsegment.h \
		gsttemplatematch.h \
		gsttextwrite.h
