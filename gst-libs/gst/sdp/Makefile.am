libgstsdpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/sdp

libgstsdpinclude_HEADERS = gstsdp.h            \
			   gstsdpmessage.h

lib_LTLIBRARIES = libgstsdp-@GST_MAJORMINOR@.la

libgstsdp_@GST_MAJORMINOR@_la_SOURCES = gstsdpmessage.c

libgstsdp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstsdp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(WIN32_LIBS)
libgstsdp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
