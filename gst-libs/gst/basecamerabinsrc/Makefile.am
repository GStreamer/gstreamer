
lib_LTLIBRARIES = libgstbasecamerabinsrc-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstbasecamerabinsrc_@GST_MAJORMINOR@_la_SOURCES = \
        gstcamerabin-enum.c \
	gstbasecamerasrc.c

libgstbasecamerabinsrc_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/basecamerabinsrc
libgstbasecamerabinsrc_@GST_MAJORMINOR@include_HEADERS = \
	gstcamerabin-enum.h \
	gstbasecamerasrc.h

libgstbasecamerabinsrc_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstbasecamerabinsrc_@GST_MAJORMINOR@_la_LIBADD = \
        $(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_MAJORMINOR@.la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) \
	-lgstinterfaces-$(GST_MAJORMINOR)

libgstbasecamerabinsrc_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

