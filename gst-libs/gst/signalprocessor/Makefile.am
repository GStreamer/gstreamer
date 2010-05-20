lib_LTLIBRARIES = libgstsignalprocessor-@GST_MAJORMINOR@.la

libgstsignalprocessor_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/signalprocessor
libgstsignalprocessor_@GST_MAJORMINOR@include_HEADERS = gstsignalprocessor.h

libgstsignalprocessor_@GST_MAJORMINOR@_la_SOURCES = gstsignalprocessor.c
libgstsignalprocessor_@GST_MAJORMINOR@_la_CFLAGS = \
    $(GST_PLUGINS_BAD_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
libgstsignalprocessor_@GST_MAJORMINOR@_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@ \
    $(GST_CONTROLLER_LIBS) $(GST_LIBS)
libgstsignalprocessor_@GST_MAJORMINOR@_la_LDFLAGS = \
    $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

