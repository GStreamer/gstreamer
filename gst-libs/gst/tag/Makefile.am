libgsttagincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tag

libgsttaginclude_HEADERS = \
	tag.h

lib_LTLIBRARIES = libgsttagedit-@GST_MAJORMINOR@.la

libgsttagedit_@GST_MAJORMINOR@_la_SOURCES = gstvorbistag.c gsttagediting.c gstid3tag.c
libgsttagedit_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgsttagedit_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) 
libgsttagedit_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LT_LDFLAGS) 

noinst_HEADERS = gsttageditingprivate.h
