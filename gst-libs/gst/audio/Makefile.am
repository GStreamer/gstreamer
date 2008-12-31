# variables used for enum/marshal generation
glib_enum_headers= 		\
	multichannel.h		\
	gstringbuffer.h

glib_enum_define=GST_AUDIO
glib_enum_prefix=gst_audio

built_sources = audio-enumtypes.c
built_headers = audio-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

lib_LTLIBRARIES = \
	libgstaudio-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstaudio_@GST_MAJORMINOR@_la_SOURCES = \
	audio.c \
	gstaudioclock.c \
	mixerutils.c \
	multichannel.c \
	gstaudiofilter.c \
	gstaudiosink.c \
	gstaudiosrc.c \
	gstbaseaudiosink.c \
	gstbaseaudiosrc.c \
	gstringbuffer.c
nodist_libgstaudio_@GST_MAJORMINOR@_la_SOURCES = $(built_sources) $(built_headers)

libgstaudio_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio.h \
	gstaudioclock.h \
	gstaudiofilter.h \
	gstaudiosink.h \
	gstaudiosrc.h \
	gstbaseaudiosink.h \
	gstbaseaudiosrc.h \
	gstringbuffer.h \
	mixerutils.h \
	multichannel.h

nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio-enumtypes.h

libgstaudio_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudio_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) \
  $(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la
libgstaudio_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_PROGRAMS = testchannels
testchannels_SOURCES = testchannels.c
testchannels_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
testchannels_LDADD = $(GST_LIBS)

include $(top_srcdir)/common/glib-gen.mak
