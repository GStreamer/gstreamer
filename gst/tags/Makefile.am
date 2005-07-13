plugin_LTLIBRARIES = libgsttagedit.la

libgsttagedit_la_SOURCES = gsttagediting.c
libgsttagedit_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgsttagedit_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttagedit_la_LIBADD = $(GST_LIBS) 

noinst_HEADERS = gsttageditingprivate.h
