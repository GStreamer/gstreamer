plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstvorbis.la

libgstvorbis_la_SOURCES = vorbis.c vorbisenc.c vorbisdec.c

## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgstvorbis_la_LIBADD = $(VORBIS_LIBS) $(VORBISENC_LIBS)
libgstvorbis_la_CFLAGS = $(VORBIS_CFLAGS) $(GST_CFLAGS)

noinst_HEADERS = vorbisenc.h vorbisdec.h
