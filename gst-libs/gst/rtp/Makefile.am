libgstrtpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtp

libgstrtpinclude_HEADERS = gstrtpbuffer.h \
			gstbasertppayload.h \
			gstbasertpdepayload.h

lib_LTLIBRARIES = libgstrtp-@GST_MAJORMINOR@.la

libgstrtp_@GST_MAJORMINOR@_la_SOURCES = gstrtpbuffer.c \
  				gstbasertppayload.c \
  				gstbasertpdepayload.c

# FIXME: what is that include doing here ?
libgstrtp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgstrtp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstrtp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LT_LDFLAGS)
