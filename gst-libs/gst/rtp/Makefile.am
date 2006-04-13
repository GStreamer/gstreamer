libgstrtpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtp

libgstrtpinclude_HEADERS = gstrtpbuffer.h \
			gstbasertppayload.h \
			gstbasertpaudiopayload.h \
			gstbasertpdepayload.h

lib_LTLIBRARIES = libgstrtp-@GST_MAJORMINOR@.la

libgstrtp_@GST_MAJORMINOR@_la_SOURCES = gstrtpbuffer.c \
  				gstbasertppayload.c \
  				gstbasertpaudiopayload.c \
  				gstbasertpdepayload.c

libgstrtp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstrtp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstrtp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
