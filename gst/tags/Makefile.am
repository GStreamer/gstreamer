
plugin_LTLIBRARIES = libgsttagedit.la

libgsttagedit_la_SOURCES = gstvorbistag.c gsttagediting.c gstid3tag.c
libgsttagedit_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgsttagedit_la_LIBADD = $(GST_LIBS) 
libgsttagedit_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsttageditingprivate.h

