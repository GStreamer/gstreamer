
plugin_LTLIBRARIES = libgstivorbis.la

libgstivorbis_la_SOURCES = vorbis.c vorbisfile.c
libgstivorbis_la_CFLAGS = $(GST_CFLAGS) $(IVORBIS_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstivorbis_la_LIBADD = $(IVORBIS_LIBS)		\
			  $(IVORBISFILE_LIBS)		\
			  $(GST_BASE_LIBS)		\
			  $(GST_PLUGINS_BASE_LIBS)	\
			  -lgstaudio-@GST_MAJORMINOR@
libgstivorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

