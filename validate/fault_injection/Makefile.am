libfaultinjection_@GST_API_VERSION@_la_SOURCES = \
	socket_interposer.c

libfaultinjection_@GST_API_VERSION@include_HEADERS = \
	socket_interposer.h

lib_LTLIBRARIES = libfaultinjection-@GST_API_VERSION@.la

libfaultinjection_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS)
libfaultinjection_@GST_API_VERSION@_la_LIBADD = $(GST_ALL_LIBS)
libfaultinjection_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/faultinjection

CLEANFILES =
