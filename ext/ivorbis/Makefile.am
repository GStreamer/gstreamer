
plugin_LTLIBRARIES = libgstivorbis.la

libgstivorbis_la_SOURCES = vorbis.c vorbisfile.c
libgstivorbis_la_CFLAGS = $(GST_CFLAGS) $(IVORBIS_CFLAGS)
libgstivorbis_la_LIBADD = $(IVORBIS_LIBS) $(IVORBISFILE_LIBS)
libgstivorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

