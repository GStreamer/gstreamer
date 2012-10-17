plugin_LTLIBRARIES = libgstandroidmedia.la

VIDEO_BASE_CLASSES_C =
	video/video.c \
	video/gstvideodecoder.c \
	video/gstvideoencoder.c \
	video/gstvideoutils.c
VIDEO_BASE_CLASSES_H = 
	video/video.h \
	video/gstvideodecoder.h \
	video/gstvideoencoder.h \
	video/gstvideoutils.h

libgstandroidmedia_la_SOURCES = \
	gst-androidmedia.c \
	gstamc.c \
	gstamcaudiodec.c \
	gstamcvideodec.c \
	video/video.c \
	video/gstvideodecoder.c \
	video/gstvideoencoder.c \
	video/gstvideoutils.c \
	gst-dvm.c \
	gst-android-hardware-camera.c \
	gst-android-graphics-surfacetexture.c \
	gst-android-graphics-imageformat.c \
	gstahcsrc.c

noinst_HEADERS = \
	gstamc.h \
	gstamc-constants.h \
	gstamcaudiodec.h \
	gstamcvideodec.h \
	video/video.h \
	video/gstvideodecoder.h \
	video/gstvideoencoder.h \
	video/gstvideoutils.h \
	gst-dvm.h \
	gst-android-hardware-camera.h \
	gst-android-graphics-surfacetexture.h \
	gst-android-graphics-imageformat.h \
	gstahcsrc.h

if !HAVE_GST_0_10_37
libgstandroidmedia_la_SOURCES += $(VIDEO_BASE_CLASSES_C)
noinst_HEADERS += $(VIDEO_BASE_CLASSES_H)
endif

libgstandroidmedia_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS) \
	$(JAVA_CFLAGS) \
	-I$(srcdir)
libgstandroidmedia_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@ \
	-lgstpbutils-@GST_MAJORMINOR@ \
	-lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstandroidmedia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
if !GST_PLUGIN_BUILD_STATIC
libgstandroidmedia_la_LIBTOOLFLAGS = --tag=disable-static
endif

EXTRA_DIST = $(VIDEO_BASE_CLASSES_C) $(VIDEO_BASE_CLASSES_H)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstandroidmedia -:SHARED libgstandroidmedia \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstandroidmedia_la_SOURCES) \
	           $(nodist_libgstandroidmedia_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstandroidmedia_la_CFLAGS) \
	 -:LDFLAGS $(libgstandroidmedia_la_LDFLAGS) \
	           $(libgstandroidmedia_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-@GST_MAJOR_MINOR@' \
	> $@
