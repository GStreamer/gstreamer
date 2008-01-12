libgsttagincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tag

libgsttaginclude_HEADERS = \
	tag.h gsttagdemux.h

lib_LTLIBRARIES = libgsttag-@GST_MAJORMINOR@.la

libgsttag_@GST_MAJORMINOR@_la_SOURCES = gstvorbistag.c gstid3tag.c tags.c gsttagdemux.c
libgsttag_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgsttag_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) 
libgsttag_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_HEADERS = gsttageditingprivate.h
