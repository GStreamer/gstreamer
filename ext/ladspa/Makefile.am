plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstladspa.la

libgstladspa_la_SOURCES = gstladspa.c search.c load.c
libgstladspa_la_CFLAGS = $(GST_CFLAGS)
libgstladspa_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstladspa.h ladspa.h utils.h
