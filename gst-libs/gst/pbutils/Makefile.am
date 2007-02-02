lib_LTLIBRARIES = libgstbaseutils-@GST_MAJORMINOR@.la

libgstbaseutils_@GST_MAJORMINOR@_la_SOURCES = \
	base-utils.c      \
	base-utils.h      \
	descriptions.c    \
	descriptions.h    \
	install-plugins.c \
	install-plugins.h \
	missing-plugins.c \
	missing-plugins.h

libgstbaseutils_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/utils
libgstbaseutils_@GST_MAJORMINOR@include_HEADERS = \
	base-utils.h      \
	descriptions.h    \
	install-plugins.h \
	missing-plugins.h

libgstbaseutils_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstbaseutils_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstbaseutils_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
