filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstossaudio.la libgstosshelper.la

libgstossaudio_la_SOURCES = gstosssink.c gstosssrc.c gstossaudio.c gstossgst.c
libgstossaudio_la_CFLAGS = $(GST_CFLAGS)

libgstosshelper_la_SOURCES = gstosshelper.c

noinst_HEADERS = gstosssink.h gstosssrc.h gstossgst.h gstosshelper.h

