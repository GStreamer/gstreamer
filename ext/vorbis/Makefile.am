plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstvorbis.la

libgstvorbis_la_SOURCES = vorbis.c vorbisenc.c vorbisdec.c
libgstvorbis_la_CFLAGS = $(GST_CFLAGS) $(VORBIS_CFLAGS)
## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgstvorbis_la_LIBADD = $(GST_LIBS) $(VORBIS_LIBS) $(VORBISENC_LIBS)

noinst_HEADERS = vorbisenc.h vorbisdec.h
