plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstossaudio.la

EXTRA_LTLIBRARIES = libgstosshelper.la

libgstossaudio_la_SOURCES = gstosssink.c gstosssrc.c gstossaudio.c gstossgst.c gstossclock.c
libgstossaudio_la_CFLAGS = $(GST_CFLAGS)
libgstossaudio_la_LIBADD =
libgstossaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstosshelper_la_SOURCES = gstosshelper.c
libgstosshelper_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstosssink.h gstosssrc.h gstossgst.h gstossclock.h gstosshelper.h
