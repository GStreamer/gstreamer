plugin_LTLIBRARIES =

if USE_VORBIS
plugin_LTLIBRARIES += libgstvorbis.la

libgstvorbis_la_SOURCES = gstvorbis.c \
			  gstvorbisdec.c \
			  gstvorbisdeclib.c \
			  gstvorbisenc.c \
			  gstvorbisparse.c \
			  gstvorbistag.c \
			  gstvorbiscommon.c

libgstvorbis_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(VORBIS_CFLAGS)
## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgstvorbis_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_LIBS) \
	$(VORBIS_LIBS) $(VORBISENC_LIBS)
libgstvorbis_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvorbis_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
endif

if USE_IVORBIS
plugin_LTLIBRARIES += libgstivorbisdec.la

libgstivorbisdec_la_SOURCES = gstivorbisdec.c \
	gstvorbisdec.c gstvorbisdeclib.c gstvorbiscommon.c
libgstivorbisdec_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	-DTREMOR $(IVORBIS_CFLAGS)
libgstivorbisdec_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_LIBS) $(IVORBIS_LIBS)
libgstivorbisdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstivorbisdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
endif

noinst_HEADERS = gstvorbisenc.h \
		 gstvorbisdec.h \
		 gstvorbisdeclib.h \
		 gstvorbisparse.h \
		 gstvorbistag.h \
		 gstvorbiscommon.h
