
plugin_LTLIBRARIES = libgsttagediting.la

libgsttagediting_la_SOURCES = gstvorbistag.c gsttagediting.c gstid3tag.c
libgsttagediting_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgsttagediting_la_LIBADD = $(GST_LIBS) 
libgsttagediting_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgsttageditingincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tags
libgsttageditinginclude_HEADERS = gsttagediting.h

noinst_HEADERS = gsttageditingprivate.h

