libgsttagincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tag

libgsttaginclude_HEADERS = \
	tag.h

lib_LTLIBRARIES = libgsttag-@GST_MAJORMINOR@.la

libgsttag_@GST_MAJORMINOR@_la_SOURCES = gstvorbistag.c gstid3tag.c
libgsttag_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgsttag_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) 
libgsttag_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LT_LDFLAGS) 

noinst_HEADERS = gsttageditingprivate.h
