plugin_LTLIBRARIES = libgstcamerabin2.la

libgstcamerabin2_la_SOURCES = gstviewfinderbin.c \
				gstdigitalzoom.c \
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
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstapp-$(GST_API_VERSION) -lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstcamerabin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcamerabin2_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstviewfinderbin.h \
		camerabingeneral.h \
		gstdigitalzoom.h \
		gstwrappercamerabinsrc.h \
		gstcamerabin2.h
