lib_LTLIBRARIES = libgstpbutils-@GST_MAJORMINOR@.la

libgstpbutils_@GST_MAJORMINOR@_la_SOURCES = \
	pbutils.c         \
	pbutils.h         \
	descriptions.c    \
	descriptions.h    \
	install-plugins.c \
	install-plugins.h \
	missing-plugins.c \
	missing-plugins.h

libgstpbutils_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/pbutils
libgstpbutils_@GST_MAJORMINOR@include_HEADERS = \
	pbutils.h         \
	descriptions.h    \
	install-plugins.h \
	missing-plugins.h

libgstpbutils_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstpbutils_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstpbutils_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
