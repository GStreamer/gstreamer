lib_LTLIBRARIES = libgstnet-@GST_MAJORMINOR@.la

libgstnet_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/net
libgstnet_@GST_MAJORMINOR@_include_HEADERS = \
    gstnet.h \
    gstnetclientclock.h \
    gstnettimepacket.h \
    gstnettimeprovider.h

libgstnet_@GST_MAJORMINOR@_la_SOURCES = \
    gstnetclientclock.c \
    gstnettimepacket.c \
    gstnettimeprovider.c

libgstnet_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstnet_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstnet_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

