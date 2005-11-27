# tempname needs to be removed as soon as the old gstnet is renamed to
# nethelper or something
lib_LTLIBRARIES = libgstnet-tempname-@GST_MAJORMINOR@.la

libgstnet_tempname_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/net
libgstnet_tempname_@GST_MAJORMINOR@_include_HEADERS = \
    gstnet.h \
    gstnetclientclock.h \
    gstnettimepacket.h \
    gstnettimeprovider.h

libgstnet_tempname_@GST_MAJORMINOR@_la_SOURCES = \
    gstnetclientclock.c \
    gstnettimepacket.c \
    gstnettimeprovider.c

libgstnet_tempname_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstnet_tempname_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstnet_tempname_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

