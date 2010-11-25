plugin_LTLIBRARIES = libgstcamerabin2.la

libgstcamerabin2_la_SOURCES = gstviewfinderbin.c \
				gstimagecapturebin.c \
				gstvideorecordingbin.c \
				camerabingeneral.c \
				gstbasecamerasrc.c \
				gstcamerabin-enum.c \
				gstv4l2camerasrc.c \
				gstplugin.c

libgstcamerabin2_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API

libgstcamerabin2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la \
	$(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR) -lgsttag-$(GST_MAJORMINOR)

libgstcamerabin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstviewfinderbin.h \
		gstimagecapturebin.h \
		gstvideorecordingbin.h \
		camerabingeneral.h \
		gstbasecamerasrc.h \
		gstv4l2camerasrc.h \
		gstcamerabin-enum.h
