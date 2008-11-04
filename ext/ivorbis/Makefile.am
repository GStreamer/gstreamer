plugin_LTLIBRARIES = libgstivorbis.la

libgstivorbis_la_SOURCES = vorbis.c vorbisfile.c vorbisdec.c
libgstivorbis_la_CFLAGS = $(GST_CFLAGS) $(IVORBIS_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstivorbis_la_LIBADD = $(IVORBIS_LIBS)		\
			  $(IVORBISFILE_LIBS)		\
			  $(GST_BASE_LIBS)		\
			  $(GST_PLUGINS_BASE_LIBS)	\
			  -lgstaudio-@GST_MAJORMINOR@
libgstivorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstivorbis_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = vorbisdec.h vorbisenc.h
