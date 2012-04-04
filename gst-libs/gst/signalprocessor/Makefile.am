lib_LTLIBRARIES = libgstsignalprocessor-@GST_API_VERSION@.la

libgstsignalprocessor_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/signalprocessor
libgstsignalprocessor_@GST_API_VERSION@include_HEADERS = gstsignalprocessor.h

libgstsignalprocessor_@GST_API_VERSION@_la_SOURCES = gstsignalprocessor.c
libgstsignalprocessor_@GST_API_VERSION@_la_CFLAGS = \
    $(GST_PLUGINS_BAD_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_CFLAGS)
libgstsignalprocessor_@GST_API_VERSION@_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
    $(GST_LIBS)
libgstsignalprocessor_@GST_API_VERSION@_la_LDFLAGS = \
    $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

