
plugin_LTLIBRARIES = libgsteffectv.la

libgsteffectv_la_SOURCES = gsteffectv.c gstedge.c gstaging.c gstdice.c gstwarp.c gstshagadelic.c gstvertigo.c gstrev.c gstquark.c
libgsteffectv_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_LIBS_CFLAGS) -I$(top_srcdir)/gst/videofilter
libgsteffectv_la_LIBADD = $(top_builddir)/gst/videofilter/libgstvideofilter.la
libgsteffectv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_PLUGINS_LIBS_LIBS) 

noinst_HEADERS = gsteffectv.h
