# variables used for enum/marshal generation
glib_enum_headers=multichannel.h
glib_enum_define=GST_AUDIO
glib_enum_prefix=gst_audio

built_sources = multichannel-enumtypes.c
built_headers = multichannel-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstaudio.la libgstaudiofilter.la
noinst_LTLIBRARIES = libgstaudiofilterexample.la

EXTRA_DIST = gstaudiofiltertemplate.c make_filter
CLEANFILES = gstaudiofilterexample.c \
	$(BUILT_SOURCES)

libgstaudio_la_SOURCES = audio.c audioclock.c \
	multichannel.c
nodist_libgstaudio_la_SOURCES = $(built_sources)

libgstaudioincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudioinclude_HEADERS = \
	audio.h \
	audioclock.h \
	gstaudiofilter.h \
	multichannel.h \
	multichannel-enumtypes.h

libgstaudio_la_LIBADD =
libgstaudio_la_CFLAGS = $(GST_CFLAGS)
libgstaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaudiofilter_la_SOURCES = gstaudiofilter.c gstaudiofilter.h
libgstaudiofilter_la_CFLAGS = $(GST_CFLAGS)
libgstaudiofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaudiofilterexample_la_SOURCES = gstaudiofilterexample.c
libgstaudiofilterexample_la_CFLAGS = $(GST_CFLAGS)
libgstaudiofilterexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

gstaudiofilterexample.c: $(srcdir)/make_filter $(srcdir)/gstaudiofiltertemplate.c
	$(srcdir)/make_filter AudiofilterExample $(srcdir)/gstaudiofiltertemplate.c

noinst_PROGRAMS = testchannels
testchannels_SOURCES = testchannels.c
testchannels_CFLAGS = $(GST_CFLAGS)
testchannels_LDFLAGS = $(GST_LIBS)

include $(top_srcdir)/common/glib-gen.mak
