# variables used for enum generation
lib_LTLIBRARIES = libgstbadaudio-@GST_API_VERSION@.la

CLEANFILES =

libgstbadaudio_@GST_API_VERSION@_la_SOURCES = \
	gstnonstreamaudiodecoder.c \
	gstplanaraudioadapter.c

nodist_libgstbadaudio_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)

libgstbadaudio_@GST_API_VERSION@_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API \
	-DBUILDING_GST_AUDIO_BAD \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(ORC_CFLAGS) \
	$(GST_AUDIO_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS)

libgstbadaudio_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_AUDIO_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)

libgstbadaudio_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstaudio_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/audio
libgstaudio_@GST_API_VERSION@include_HEADERS = \
	gstnonstreamaudiodecoder.h \
	audio-bad-prelude.h \
	gstplanaraudioadapter.h
