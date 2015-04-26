
lib_LTLIBRARIES = libgstbasecamerabinsrc-@GST_API_VERSION@.la

CLEANFILES = $(BUILT_SOURCES)

libgstbasecamerabinsrc_@GST_API_VERSION@_la_SOURCES = \
        gstcamerabin-enum.c \
        gstcamerabinpreview.c \
	gstbasecamerasrc.c

libgstbasecamerabinsrc_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/basecamerabinsrc
libgstbasecamerabinsrc_@GST_API_VERSION@include_HEADERS = \
	gstcamerabin-enum.h \
	gstcamerabinpreview.h \
	gstbasecamerasrc.h

libgstbasecamerabinsrc_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstbasecamerabinsrc_@GST_API_VERSION@_la_LIBADD = \
	-lgstapp-$(GST_API_VERSION) $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)

libgstbasecamerabinsrc_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
