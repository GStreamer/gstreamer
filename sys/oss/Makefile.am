plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstossaudio.la libgstosshelper.la

libgstossaudio_la_SOURCES = gstosssink.c gstosssrc.c gstossaudio.c gstossgst.c
libgstossaudio_la_CFLAGS = $(GST_CFLAGS)
libgstossaudio_la_LIBADD = $(GST_LIBS)

libgstosshelper_la_SOURCES = gstosshelper.c

noinst_HEADERS = gstosssink.h gstosssrc.h gstossgst.h gstosshelper.h

