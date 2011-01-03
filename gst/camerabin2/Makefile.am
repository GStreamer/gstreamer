plugin_LTLIBRARIES = libgstcamerabin2.la

libgstcamerabin2_la_SOURCES = gstviewfinderbin.c \
				gstimagecapturebin.c \
				gstvideorecordingbin.c \
				camerabingeneral.c \
				gstwrappercamerabinsrc.c \
				gstcamerabin2.c \
				gstplugin.c

libgstcamerabin2_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-DGST_USE_UNSTABLE_API

libgstcamerabin2_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-$(GST_MAJORMINOR).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstcamerabin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstviewfinderbin.h \
		gstimagecapturebin.h \
		gstvideorecordingbin.h \
		camerabingeneral.h \
		gstwrappercamerabinsrc.h \
		gstcamerabin2.h
