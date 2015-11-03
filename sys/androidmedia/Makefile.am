plugin_LTLIBRARIES = libgstandroidmedia.la

libgstandroidmedia_la_SOURCES = \
	gstamc.c \
	gstamcaudiodec.c \
	gstamcvideodec.c \
	gstamcvideoenc.c \
	gstamcsurface.c \
	gstamcsurfacetexture.c \
	gstjniutils.c

noinst_HEADERS = \
	gstamc.h \
	gstamc-constants.h \
	gstamcaudiodec.h \
	gstamcvideodec.h \
	gstamcvideoenc.h \
	gstamcsurface.h \
	gstamcsurfacetexture.h \
	gstjniutils.h

libgstandroidmedia_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)
libgstandroidmedia_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)
libgstandroidmedia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstandroidmedia_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

androidmedia_java_classesdir = $(datadir)/gst-android/ndk-build/androidmedia/
androidmedia_java_classes_DATA = org/freedesktop/gstreamer/androidmedia/GstAmcOnFrameAvailableListener.java
