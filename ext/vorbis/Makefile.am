
plugin_LTLIBRARIES = libgstvorbis.la

libgstvorbis_la_SOURCES = vorbis.c vorbisdec.c vorbisenc.c oggvorbisenc.c 
libgstvorbis_la_CFLAGS = $(GST_CFLAGS) $(VORBIS_CFLAGS) 
## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgstvorbis_la_LIBADD = $(VORBIS_LIBS) $(VORBISENC_LIBS) $(VORBISFILE_LIBS) 
libgstvorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = vorbisenc.h vorbisdec.h oggvorbisenc.h
